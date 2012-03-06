#pragma once

#include <boost/thread/mutex.hpp>

extern const uint32 kM6DiskPageSize;

class M6File;

// --------------------------------------------------------------------

struct M6DiskPageInfo;
typedef M6DiskPageInfo*	M6DiskPageInfoPtr;

class M6DiskCache
{
  public:
	static M6DiskCache&	Instance();
	
	void*				Load(M6File& inFile, int64 inOffset);

	void				Reference(void* inPage);
	void				Release(void* inPage, bool inDirty);
	void				Swap(void* inPageA, void* inPageB);

	void				Flush(M6File& inFile);
	void				Purge(M6File& inFile);
	
	void				Truncate(M6File& inFile, int64 inSize);
	
  private:
						M6DiskCache();
						~M6DiskCache();

	uint8*				mData;
	M6DiskPageInfoPtr	mCache, mLRUHead, mLRUTail;
	std::vector<uint32>*mBuckets;
	boost::mutex		mMutex;
};

