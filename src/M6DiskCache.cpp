#include "M6Lib.h"

#include <boost/functional/hash.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6File.h"
#include "M6DiskCache.h"
#include "M6Error.h"

using namespace std;

const uint32
	kM6DiskPageSize = 8192,
//	kM6DiskPageSize = 512,
	kM6DiskCacheSize = 65536,
//	kM6DiskCacheSize = 128,
	kM6BucketCount = 4 * kM6DiskCacheSize;

// --------------------------------------------------------------------

#if defined(_MSC_VER)

#include <Windows.h>

void lock_memory(void* ptr, size_t size)
{
	::VirtualLock(ptr, size);
}

#elif defined(linux) || defined(__linux__)

#include <sys/mman.h>

void lock_memory(void* ptr, size_t size)
{
	::mlock(ptr, size);
}

#else
#	error "Implement mlock for this OS"
#endif

size_t hash_value(M6Handle inFileHandle, int64 inOffset)
{
	size_t result = 0;
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
	uint32			mLink;
	bool			mDirty;
};

inline size_t hash_value(const M6DiskPageInfo& inInfo)
{
	return hash_value(inInfo.mFileHandle, inInfo.mOffset);
}

M6DiskCache& M6DiskCache::Instance()
{
	static M6DiskCache sInstance;
	return sInstance;
}

M6DiskCache::M6DiskCache()
	: mData(new uint8[kM6DiskPageSize * kM6DiskCacheSize])
	, mCache(new M6DiskPageInfo[kM6DiskCacheSize])
	, mBuckets(new uint32[kM6BucketCount])
{
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		mCache[ix].mFileHandle = -1;
		mCache[ix].mOffset = 0;
		mCache[ix].mRefCount = 0;
		mCache[ix].mDirty = false;
		mCache[ix].mNext = mCache + ix + 1;
		mCache[ix].mPrev = mCache + ix - 1;
		mCache[ix].mLink = kM6DiskCacheSize;
	}
	
	mCache[0].mPrev = nullptr;
	mCache[kM6DiskCacheSize - 1].mNext = nullptr;
	
	mLRUHead = mCache;
	mLRUTail = mCache + kM6DiskCacheSize - 1;
	
	fill(mBuckets, mBuckets + kM6BucketCount, kM6DiskCacheSize);
	
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
	boost::mutex::scoped_lock lock(mMutex);
	
	assert(inOffset % kM6DiskPageSize == 0);

	uint8* result = nullptr;
	M6DiskPageInfoPtr info = nullptr;

	// Try to find the page in our cache
	size_t bucket = hash_value(inFile.GetHandle(), inOffset) % kM6BucketCount;
	uint32 index = mBuckets[bucket];
	while (index < kM6DiskCacheSize)
	{
		if (mCache[index].mFileHandle == inFile.GetHandle() and
			mCache[index].mOffset == inOffset)
		{
			result = mData + index * kM6DiskPageSize;

			info = mCache + index;
			info->mRefCount += 1;

			break;
		}
		
		index = mCache[index].mLink;
	}
	
	if (result == nullptr)
	{
		info = mLRUTail;
		
		// now search backwards for a cached page that can be recycled
		while (info != nullptr and info->mRefCount > 0)
			info = info->mPrev;
	
		// we could end up with a full cache, if so, PANIC!!!
		// (of course, a panic is not really needed, there are better ways to handle this)
		if (info == nullptr)
			THROW(("Panic: disk cache full"));
	
		size_t index = info - mCache;
		result = mData + index * kM6DiskPageSize;
		
		if (info->mFileHandle >= 0)
			PurgePage(index);
		
		info->mFileHandle = inFile.GetHandle();
		info->mOffset = inOffset;
		info->mDirty = false;
		info->mRefCount = 1;
		info->mLink = mBuckets[bucket];

		mBuckets[bucket] = index;
		
		M6IO::pread(info->mFileHandle, result, kM6DiskPageSize, info->mOffset);
	}
	
	assert(info != nullptr);
	
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

void M6DiskCache::Touch(void *inPage)
{
	boost::mutex::scoped_lock lock(mMutex);

	size_t index = (static_cast<uint8*>(inPage) - mData) / kM6DiskPageSize;
	if (index >= kM6DiskCacheSize)
		THROW(("Invalid page for Reference"));
	mCache[index].mDirty = true;
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
//#if DEBUG
//	else if (not mCache[index].mDirty)
//	{
//		// make sure it really isn't dirty
//		uint8 data[kM6DiskPageSize];
//		M6IO::pread(mCache[index].mFileHandle, data, kM6DiskPageSize, mCache[index].mOffset);
//		assert(memcmp(data, inPage, kM6DiskPageSize) == 0);
//	}
//#endif
}

void M6DiskCache::Swap(void* inPageA, void* inPageB)
{
	boost::mutex::scoped_lock lock(mMutex);

	size_t indexA = (static_cast<uint8*>(inPageA) - mData) / kM6DiskPageSize;
	if (indexA >= kM6DiskCacheSize)
		THROW(("Invalid page for Swap"));
	size_t bucketA = hash_value(mCache[indexA]) % kM6BucketCount;
	
	if (mBuckets[bucketA] == indexA)
		mBuckets[bucketA] = mCache[indexA].mLink;
	else
	{
		uint32 ix = mBuckets[bucketA];
		while (mCache[ix].mLink != indexA)
		{
			ix = mCache[ix].mLink;
			assert(ix < kM6DiskCacheSize);
		}
		mCache[ix].mLink = mCache[indexA].mLink;
	}

	size_t indexB = (static_cast<uint8*>(inPageB) - mData) / kM6DiskPageSize;
	if (indexB >= kM6DiskCacheSize)
		THROW(("Invalid page for Swap"));
	size_t bucketB = hash_value(mCache[indexB]) % kM6BucketCount;

	if (mBuckets[bucketB] == indexB)
		mBuckets[bucketB] = mCache[indexB].mLink;
	else
	{
		uint32 ix = mBuckets[bucketB];
		while (mCache[ix].mLink != indexB)
		{
			ix = mCache[ix].mLink;
			assert(ix < kM6DiskCacheSize);
		}
		mCache[ix].mLink = mCache[indexB].mLink;
	}

	assert(mCache[indexA].mFileHandle == mCache[indexB].mFileHandle);
	swap(mCache[indexA].mOffset, mCache[indexB].mOffset);
	swap(mCache[indexA].mRefCount, mCache[indexB].mRefCount);
	mCache[indexA].mDirty = mCache[indexB].mDirty = true;
	
	mCache[indexB].mLink = mBuckets[bucketA];
	mBuckets[bucketA] = indexB;
	assert(hash_value(mCache[indexB]) % kM6BucketCount == bucketA);

	mCache[indexA].mLink = mBuckets[bucketB];
	mBuckets[bucketB] = indexA;
	assert(hash_value(mCache[indexA]) % kM6BucketCount == bucketB);
}

void M6DiskCache::Flush(M6File& inFile)
{
	boost::mutex::scoped_lock lock(mMutex);

	M6Handle handle = inFile.GetHandle();
	
	// avoid scattering around the disk, write data sorted
	vector<int64> offsets;
	offset.reserve(kM6DiskCacheSize);
	
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		if (mCache[ix].mDirty and mCache[ix].mFileHandle == handle)
		{
			offsets.push_back(ix);
			mCache[ix].mDirty = false;
		}
	}
	
	sort(offsets.begin(), offsets.end());
	
	foreach (int64 offset, offsets)
	{
		uint8* page = mData + ix * kM6DiskPageSize;
		assert(*page != 0);
		M6IO::pwrite(mCache[ix].mFileHandle, page, kM6DiskPageSize, mCache[ix].mOffset);
	}
}

void M6DiskCache::Purge(M6File& inFile)
{
	boost::mutex::scoped_lock lock(mMutex);

	M6Handle handle = inFile.GetHandle();
	
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		if (mCache[ix].mFileHandle == handle)
			PurgePage(ix);
	}
}

void M6DiskCache::PurgePage(uint32 inPage)
{
	assert(mCache[inPage].mFileHandle >= 0);
	assert(mCache[inPage].mRefCount == 0);

	if (mCache[inPage].mDirty)
	{
		uint8* page = mData + inPage * kM6DiskPageSize;
		assert(*page != 0);
		M6IO::pwrite(mCache[inPage].mFileHandle, page, kM6DiskPageSize, mCache[inPage].mOffset);
	}
	
	size_t bucket = hash_value(mCache[inPage]) % kM6BucketCount;
	uint32 index = mBuckets[bucket];
	assert(index < kM6DiskCacheSize);
	uint32 next = mCache[index].mLink;
	
	if (index == inPage)
		mBuckets[bucket] = next;
	else
	{
		while (next != inPage and next != kM6DiskCacheSize)
		{
			index = next;
			next = mCache[index].mLink;
		}
		
		assert(next == inPage);
		mCache[index].mLink = mCache[next].mLink;
	}
	
	mCache[inPage].mFileHandle = -1;
	mCache[inPage].mOffset = 0;
	mCache[inPage].mDirty = false;
	mCache[inPage].mRefCount = 0;
	mCache[inPage].mLink = kM6DiskCacheSize;
}

void M6DiskCache::Truncate(M6File& inFile, int64 inSize)
{
	boost::mutex::scoped_lock lock(mMutex);

	M6Handle handle = inFile.GetHandle();
	
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		if (mCache[ix].mFileHandle == handle and mCache[ix].mOffset >= inSize)
			PurgePage(ix);
	}
}

//void M6DiskCache::Validate()
//{
//	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
//	{
//		if (mCache[ix].mFileHandle >= 0)
//		{
//			uint32 bucket = hash_value(mCache[ix]) % kM6BucketCount;
//			
//			uint32 i = mBuckets[bucket];
//			while (i != ix)
//			{
//				i = mCache[i].mLink;
//				assert(i < kM6DiskCacheSize);
//			}
//			assert(i == ix);
//		}
//	}
//	
//	for (uint32 ix = 0; ix < kM6BucketCount; ++ix)
//	{
//		uint32 i = mBuckets[ix];
//		while (i != kM6DiskCacheSize)
//		{
//			assert(hash_value(mCache[i]) % kM6BucketCount == ix);
//			i = mCache[i].mLink;
//		}
//	}
//}
