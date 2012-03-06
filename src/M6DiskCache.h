#pragma once

extern const uint32 kM6DiskPageSize;

class M6File;

class M6DiskPage
{
  public:
						M6DiskPage(M6File& inFile, int64 inOffset);
						M6DiskPage(const M6DiskPage& inPage);
	M6DiskPage&			operator=(const M6DiskPage& inPage);
						~M6DiskPage();
  protected:
	void*				mPage;
	bool				mDirty;
};

//template<class T>
//class M6DiskPageT
//{
//  public:
//						M6DiskPageT(M6File& inFile, int64 inOffset)
//							: M6DiskPage(inFile, inOffset) {}
//						M6DiskPageT(const M6DiskPageT& inPage)
//							: M6DiskPage(inPage) {}
//
//	M6DiskPageT&		operator=(const M6DiskPageT& inPage)
//						{
//							M6DiskPage::operator=(inPage);
//							return *this;
//						}
//
//	const T&			operator*() const	{ return *static_cast<T*>(mPage); }
//	const T*			operator->() const	{ return static_cast<T*>(mPage); }
//
//	T&					operator*()			{ mDirty = true; return *static_cast<T*>(mPage); }
//	T*					operator->()		{ mDirty = true; return static_cast<T*>(mPage); }
//};

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

	void				Flush(M6File& inFile);
	void				Purge(M6File& inFile);
	
  private:
						M6DiskCache();
						~M6DiskCache();

	void				EmptyBucket(uint32 inIndex);

	uint8*				mData;
	M6DiskPageInfoPtr	mCache, mLRUHead, mLRUTail;
	uint32*				mBuckets;
};

