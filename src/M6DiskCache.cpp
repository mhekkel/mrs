#include "M6Lib.h"

#include <boost/functional/hash.hpp>

#include "M6File.h"
#include "M6DiskCache.h"
#include "M6Error.h"

using namespace std;

const uint32
	kM6DiskPageSize = 8192,
	kM6DiskCacheSize = 65536,
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


// --------------------------------------------------------------------

struct M6DiskPageInfo
{
	M6DiskPageInfo*	mNext;
	M6DiskPageInfo*	mPrev;
	M6File			mFile;
	int64			mOffset;
	uint32			mRefCount;
	bool			mDirty;
};

size_t hash_value(const M6DiskPageInfo& inInfo)
{
	size_t result = 0;
	boost::hash_combine(result, inInfo.mFile.GetHandle());
	boost::hash_combine(result, inInfo.mOffset);
	return result;
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
		mCache[ix].mOffset = 0;
		mCache[ix].mRefCount = 0;
		mCache[ix].mDirty = false;
		mCache[ix].mNext = mCache + ix + 1;
		mCache[ix].mPrev = mCache + ix - 1;
	}
	
	mLRUHead = mCache;
	mLRUTail = mCache + kM6DiskCacheSize - 1;
	
	fill(mBuckets, mBuckets + kM6BucketCount, kM6DiskCacheSize);
	
	// make sure our cache is never swapped out
	lock_memory(mBuckets, sizeof(uint32) * kM6BucketCount);
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
	// Try to find the page in our cache
	size_t bucket = 0;
	boost::hash_combine(bucket, inFile.GetHandle());
	boost::hash_combine(bucket, inOffset);

	bucket = bucket % kM6BucketCount;
	
	void* result = nullptr;
	while (result == nullptr)
	{
		uint32 index = mBuckets[bucket];
		if (index == kM6DiskCacheSize)	// not available
			break;
		
		if (mCache[index].mFile.GetHandle() == inFile.GetHandle() and
			mCache[index].mOffset == inOffset)
		{
			result = mData + index * kM6DiskPageSize;
			mCache[index].mRefCount += 1;
			break;
		}
		
		bucket = (bucket + 1) % kM6BucketCount;
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
			info->mFile.PWrite(result, kM6DiskPageSize, info->mOffset);
			EmptyBucket(index);
		}
		
		inFile.PRead(result, kM6DiskPageSize, inOffset);

		info->mFile = inFile;
		info->mOffset = inOffset;
		info->mDirty = false;
		info->mRefCount = 1;
		
		mBuckets[bucket] = static_cast<uint32>(index);
	}
	
	return result;
}

void M6DiskCache::Reference(void* inPage)
{
	size_t index = (static_cast<uint8*>(inPage) - mData) / kM6DiskPageSize;
	if (index >= kM6DiskCacheSize)
		THROW(("Invalid page for Reference"));
	mCache[index].mRefCount += 1;
}

void M6DiskCache::Release(void* inPage, bool inDirty)
{
	size_t index = (static_cast<uint8*>(inPage) - mData) / kM6DiskPageSize;
	if (index >= kM6DiskCacheSize)
		THROW(("Invalid page for Reference"));
	
	mCache[index].mRefCount -= 1;
	if (inDirty)
		mCache[index].mDirty = true;
}

void M6DiskCache::Flush(M6File& inFile)
{
	M6Handle handle = inFile.GetHandle();
	
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		if (mCache[ix].mDirty and mCache[ix].mFile.GetHandle() == handle)
		{
			void* page = mData + ix * kM6DiskPageSize;
			mCache[ix].mFile.PWrite(page, kM6DiskPageSize, mCache[ix].mOffset);
			mCache[ix].mDirty = false;
		}
	}
}

void M6DiskCache::Purge(M6File& inFile)
{
	M6Handle handle = inFile.GetHandle();
	
	for (uint32 ix = 0; ix < kM6DiskCacheSize; ++ix)
	{
		if (mCache[ix].mFile.GetHandle() == handle)
		{
			if (mCache[ix].mDirty)
			{
				void* page = mData + ix * kM6DiskPageSize;
				mCache[ix].mFile.PWrite(page, kM6DiskPageSize, mCache[ix].mOffset);
			}

			EmptyBucket(ix);

			mCache[ix].mDirty = false;
			mCache[ix].mFile = M6File();
		}
	}
}

void M6DiskCache::EmptyBucket(uint32 inIndex)
{
	size_t hash = 0;
	boost::hash_combine(hash, mCache[inIndex].mFile.GetHandle());
	boost::hash_combine(hash, mCache[inIndex].mOffset);
	
	size_t bucket = hash % kM6BucketCount;
	
	while (mBuckets[bucket] != inIndex and mBuckets[bucket] != kM6DiskPageSize)
		bucket = (bucket + 1) % kM6BucketCount;
	
	if (mBuckets[bucket] == kM6DiskPageSize)
		THROW(("Bucket not found!"));

	// find collisions, and shift them up if any
	size_t next = (bucket + 1) % kM6BucketCount;
	while (mBuckets[next] != kM6DiskPageSize)
	{
		size_t h2 = 0;
		boost::hash_combine(h2, mCache[mBuckets[next]].mFile.GetHandle());
		boost::hash_combine(h2, mCache[mBuckets[next]].mOffset);
		if (h2 == hash)
		{
			mBuckets[bucket] = mBuckets[next];
			bucket = next;
		}
		next = (next + 1) % kM6BucketCount;
	}

	mBuckets[bucket] = kM6DiskPageSize;
}

// --------------------------------------------------------------------

M6DiskPage::M6DiskPage(M6File& inFile, int64 inOffset)
	: mPage(M6DiskCache::Instance().Load(inFile, inOffset))
	, mDirty(false)
{
}

M6DiskPage::M6DiskPage(const M6DiskPage& inPage)
	: mPage(inPage.mPage)
	, mDirty(inPage.mDirty)
{
	M6DiskCache::Instance().Reference(mPage);
}

M6DiskPage& M6DiskPage::operator=(const M6DiskPage& inPage)
{
	if (this != &inPage)
	{
		M6DiskCache::Instance().Release(mPage, mDirty);
		mPage = inPage.mPage;
		mDirty = inPage.mDirty;
		M6DiskCache::Instance().Reference(mPage);
	}
	
	return *this;
}

M6DiskPage::~M6DiskPage()
{
	M6DiskCache::Instance().Release(mPage, mDirty);
}



