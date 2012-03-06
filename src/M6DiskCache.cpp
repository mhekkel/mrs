#include "M6Lib.h"

#include <boost/functional/hash.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6File.h"
#include "M6DiskCache.h"
#include "M6Error.h"

using namespace std;

const uint32
	//kM6DiskPageSize = 8192,
	kM6DiskPageSize = 512,
	//kM6DiskCacheSize = 65536,
	kM6DiskCacheSize = 128,
	kM6BucketCount = 4 * kM6DiskCacheSize;

// --------------------------------------------------------------------

#if defined(_MSC_VER)

#include <Windows.h>

void lock_memory(void* ptr, size_t size)
{
	::VirtualLock(ptr, size);
}

#elif defined(linux)

void lock_memory(void* ptr, size_t size)
{
	::mlock(ptr, size);
}

#else
#	error "Implement mlock for this OS"
#endif

size_t hash_value(M6Handle inFileHandle, int64 inOffset)
{
	size_t result;
	boost::hash_combine(result, inFileHandle);
	boost::hash_combine(result, inOffset / kM6DiskPageSize);
	return result;
}

// --------------------------------------------------------------------

struct M6DiskPageInfo
{
	M6DiskPageInfo*	mNext;
	M6DiskPageInfo*	mPrev;
	M6Handle		mFileHandle;
	int64			mOffset;
	uint32			mRefCount;
	bool			mDirty;
};

M6DiskCache& M6DiskCache::Instance()
{
	static M6DiskCache sInstance;
	return sInstance;
}

M6DiskCache::M6DiskCache()
	: mData(new uint8[kM6DiskPageSize * kM6DiskCacheSize])
	, mCache(new M6DiskPageInfo[kM6DiskCacheSize])
	, mBuckets(new vector<uint32>[kM6BucketCount])
{
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		mCache[ix].mOffset = 0;
		mCache[ix].mRefCount = 0;
		mCache[ix].mDirty = false;
		mCache[ix].mNext = mCache + ix + 1;
		mCache[ix].mPrev = mCache + ix - 1;
	}
	
	mLRUHead = mCache;
	mLRUTail = mCache + kM6DiskCacheSize - 1;
	
	// make sure our cache is never swapped out
	lock_memory(mBuckets, sizeof(vector<uint32>) * kM6BucketCount);
	lock_memory(mData, kM6DiskPageSize * kM6DiskCacheSize);
	lock_memory(mCache, sizeof(M6DiskPageInfo) * kM6DiskCacheSize);
}

M6DiskCache::~M6DiskCache()
{
#if DEBUG
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		assert(mCache[ix].mRefCount == 0);
		assert(mCache[ix].mDirty == false);
	}
#endif

	delete [] mCache;
	delete [] mData;
	delete [] mBuckets;
}

void* M6DiskCache::Load(M6File& inFile, int64 inOffset)
{
	assert(inOffset % kM6DiskPageSize == 0);

	boost::mutex::scoped_lock lock(mMutex);
	
	// Try to find the page in our cache
	size_t bucket = hash_value(inFile.GetHandle(), inOffset);
	bucket = bucket % kM6BucketCount;
	
	void* result = nullptr;
	
	if (not mBuckets[bucket].empty())
	{
		foreach (uint32 index, mBuckets[bucket])
		{
			if (mCache[index].mFileHandle == inFile.GetHandle() and
				mCache[index].mOffset == inOffset)
			{
				result = mData + index * kM6DiskPageSize;
				mCache[index].mRefCount += 1;
				break;
			}
		}
	}
	
	if (result == nullptr)
	{
		M6DiskPageInfoPtr info = mLRUTail;
		
		// now search backwards for a cached page that can be recycled
		while (info != nullptr and info->mRefCount > 0)
			info = info->mPrev;
	
		// we could end up with a full cache, if so, PANIC!!!
		if (info == nullptr)
			THROW(("Panic: disk cache full"));
	
		if (info != mLRUHead)
		{
			if (info == mLRUTail)
				mLRUTail = info->mPrev;
			
			if (info->mPrev)
				info->mPrev->mNext = info->mNext;
			if (info->mNext)
				info->mNext->mPrev = info->mPrev;
			
			info->mPrev = nullptr;
			info->mNext = mLRUHead;
			mLRUHead->mPrev = info;
			mLRUHead = info;
		}
		
		size_t index = info - mCache;
		result = mData + index * kM6DiskPageSize;
		
		if (info->mDirty)
		{
			M6IO::pwrite(info->mFileHandle, result, kM6DiskPageSize, info->mOffset);
			mBuckets[bucket].erase(
				remove(mBuckets[bucket].begin(), mBuckets[bucket].end(), index),
				mBuckets[bucket].end());
		}
		
		info->mFileHandle = inFile.GetHandle();
		info->mOffset = inOffset;
		info->mDirty = false;
		info->mRefCount = 1;
		
		M6IO::pread(info->mFileHandle, result, kM6DiskPageSize, info->mOffset);

		mBuckets[bucket].push_back(index);
	}
	
	return result;
}

void M6DiskCache::Reference(void* inPage)
{
	boost::mutex::scoped_lock lock(mMutex);

	size_t index = (static_cast<uint8*>(inPage) - mData) / kM6DiskPageSize;
	if (index >= kM6DiskCacheSize)
		THROW(("Invalid page for Reference"));
	mCache[index].mRefCount += 1;
}

void M6DiskCache::Release(void* inPage, bool inDirty)
{
	boost::mutex::scoped_lock lock(mMutex);

	size_t index = (static_cast<uint8*>(inPage) - mData) / kM6DiskPageSize;
	if (index >= kM6DiskCacheSize)
		THROW(("Invalid page for Release"));
	
	mCache[index].mRefCount -= 1;
	if (inDirty)
		mCache[index].mDirty = true;
}

void M6DiskCache::Swap(void* inPageA, void* inPageB)
{
	boost::mutex::scoped_lock lock(mMutex);

	size_t indexA = (static_cast<uint8*>(inPageA) - mData) / kM6DiskPageSize;
	if (indexA >= kM6DiskCacheSize)
		THROW(("Invalid page for Swap"));

	size_t indexB = (static_cast<uint8*>(inPageB) - mData) / kM6DiskPageSize;
	if (indexB >= kM6DiskCacheSize)
		THROW(("Invalid page for Swap"));

	size_t bucketA = hash_value(mCache[indexA].mFileHandle, mCache[indexA].mOffset);
	bucketA %= kM6BucketCount;

	size_t bucketB = hash_value(mCache[indexB].mFileHandle, mCache[indexB].mOffset);
	bucketB %= kM6BucketCount;

	assert(mCache[indexA].mFileHandle == mCache[indexB].mFileHandle);
	swap(mCache[indexA].mOffset, mCache[indexB].mOffset);
	swap(mCache[indexA].mRefCount, mCache[indexB].mRefCount);
	mCache[indexA].mDirty = mCache[indexB].mDirty = true;

	// swap bucket entries
	mBuckets[bucketA].erase(remove(mBuckets[bucketA].begin(), mBuckets[bucketA].end(), indexA), mBuckets[bucketA].end());
	mBuckets[bucketB].erase(remove(mBuckets[bucketB].begin(), mBuckets[bucketB].end(), indexB), mBuckets[bucketB].end());

	mBuckets[bucketA].push_back(indexB);
	mBuckets[bucketB].push_back(indexA);
}

void M6DiskCache::Flush(M6File& inFile)
{
	boost::mutex::scoped_lock lock(mMutex);

	M6Handle handle = inFile.GetHandle();
	
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		if (mCache[ix].mDirty and mCache[ix].mFileHandle == handle)
		{
			void* page = mData + ix * kM6DiskPageSize;
			M6IO::pwrite(mCache[ix].mFileHandle, page, kM6DiskPageSize, mCache[ix].mOffset);
			mCache[ix].mDirty = false;
		}
	}
}

void M6DiskCache::Purge(M6File& inFile)
{
	boost::mutex::scoped_lock lock(mMutex);

	M6Handle handle = inFile.GetHandle();
	
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		if (mCache[ix].mFileHandle == handle)
		{
			if (mCache[ix].mDirty)
			{
				void* page = mData + ix * kM6DiskPageSize;
				M6IO::pwrite(mCache[ix].mFileHandle, page, kM6DiskPageSize, mCache[ix].mOffset);
			}
			
			size_t hash = hash_value(mCache[ix].mFileHandle, mCache[ix].mOffset);
			size_t bucket = hash % kM6BucketCount;

			mBuckets[bucket].erase(
				remove(mBuckets[bucket].begin(), mBuckets[bucket].end(), ix),
				mBuckets[bucket].end());

			mCache[ix].mDirty = false;
			mCache[ix].mFileHandle = -1;
		}
	}
}
