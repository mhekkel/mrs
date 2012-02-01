//	Document store. Store documents in pages and keep an index
//	to the start of each document.
//	The total size of a doc store is limited to 32 tera bytes.

#include "M6Lib.h"

#include <cassert>

#include "M6DocStore.h"
#include "M6Document.h"
#include "M6Error.h"

using namespace std;

// --------------------------------------------------------------------

const uint32
	kM6DocStoreSignature	= 'm6ds',
//	kM6DataPageSize			= 16384,
	kM6DataPageSize			= 256,
	kM6DataPageTextSize		= kM6DataPageSize - 8,
	kM6DataPageIndexCount	= kM6DataPageTextSize / (3 * sizeof(uint32)),
	kM6DataPageTextCutOff	= 64;	// start a new data page if 'free' is less than this

enum M6DataPageType
{
	eM6DocStoreEmptyPage,
	eM6DocStoreDataPage,
	eM6DocStoreIndexLeafPage,
	eM6DocStoreIndexBranchPage,
};


struct M6DocStoreIndexEntry
{
	uint32	mDocNr;
	uint32	mPageNr;
	uint32	mDocSize;
};

struct M6DocStorePageData
{
	uint8			mType;
	uint8			mFlags;
	uint16			mN;
	uint32			mLink;
	union
	{
		M6DocStoreIndexEntry
					mData[kM6DataPageIndexCount];
		uint8		mText[kM6DataPageTextSize];
	};

	template<class Archive>
	void serialize(Archive& ar)
	{
		ar & mType & mFlags & mN & mLink & mText;
	}
};

BOOST_STATIC_ASSERT(sizeof(M6DocStorePageData) == kM6DataPageSize);

struct M6DocStoreHdr
{
	uint32			mSignature;
	uint32			mHeaderSize;
	uint32			mDocCount;
	uint32			mIndexRoot;
	uint32			mNextDocNumber;
	uint32			mFirstDataPage;
	uint32			mLastDataPage;
	uint32			mFirstFreeDataPage;

	template<class Archive>
	void serialize(Archive& ar)
	{
		ar & mSignature & mHeaderSize & mDocCount & mIndexRoot & mNextDocNumber
		   & mFirstDataPage & mLastDataPage & mFirstFreeDataPage;
	}
};

union M6DocStoreHdrPage
{
	M6DocStoreHdr	mHeader;
	uint8			mFiller[kM6DataPageSize];
};

// --------------------------------------------------------------------

class M6DocStorePage
{
  public:
					M6DocStorePage(M6DocStoreImpl& inStore, M6DocStorePageData* inData, uint32 inPageNr)
						: mStore(inStore), mData(inData), mPageNr(inPageNr), mDirty(false) {}
	virtual			~M6DocStorePage() {}

	void			SetPageType(uint8 inType)		{ mData->mType = inType; }

	void			SetLink(uint32 inPageNr)		{ mData->mLink = inPageNr; }
	uint32			GetLink() const					{ return mData->mLink; }

	void			SetDirty(bool inDirty)			{ mDirty = inDirty; }
	bool			IsDirty() const					{ return mDirty; }

	uint32			GetPageNr() const				{ return mPageNr; }

	void			Flush(M6File& inFile);

  protected:

	M6DocStoreImpl&			mStore;
	M6DocStorePageData*		mData;
	uint32					mPageNr;
	bool					mDirty;

  private:
					M6DocStorePage(const M6DocStorePage&);
	M6DocStorePage&	operator=(const M6DocStorePage&);
};

// --------------------------------------------------------------------

class M6DocStoreDataPage : public M6DocStorePage
{
  public:
					M6DocStoreDataPage(M6DocStoreImpl& inStore, M6DocStorePageData* inData, uint32 inPageNr);
	virtual			~M6DocStoreDataPage();

	uint32			Store(uint32 inDocNr, const uint8* inData, uint32 inSize);
	uint32			Load(uint32 inDocNr, uint8* outData, uint32 inSize);

  private:
	
	void			Write16(uint8*& ioPtr, uint16 inValue)
					{
						assert(ioPtr < mData->mText + kM6DataPageTextSize - 2);
						*ioPtr++ = static_cast<uint8>(inValue >>  8);
						*ioPtr++ = static_cast<uint8>(inValue >>  0);
					}

	void			Write32(uint8*& ioPtr, uint32 inValue)
					{
						assert(ioPtr < mData->mText + kM6DataPageTextSize - 4);
						*ioPtr++ = static_cast<uint8>(inValue >> 24);
						*ioPtr++ = static_cast<uint8>(inValue >> 16);
						*ioPtr++ = static_cast<uint8>(inValue >>  8);
						*ioPtr++ = static_cast<uint8>(inValue >>  0);
					}

	uint32			Read16(uint8*& ioPtr)
					{
						assert(ioPtr <= mData->mText + mData->mN - 2);

						uint32 result = *ioPtr++;
						result = result << 8 | *ioPtr++;
						
						return result;
					}

	uint32			Read32(uint8*& ioPtr)
					{
						assert(ioPtr <= mData->mText + mData->mN - 4);

						uint32 result = *ioPtr++;
						result = result << 8 | *ioPtr++;
						result = result << 8 | *ioPtr++;
						result = result << 8 | *ioPtr++;
						
						return result;
					}
};

// --------------------------------------------------------------------

class M6DocStoreIndexPage : public M6DocStorePage
{
  public:
					M6DocStoreIndexPage(M6DocStoreImpl& inStore, M6DocStorePageData* inData, uint32 inPageNr);
	virtual			~M6DocStoreIndexPage();

	bool			Insert(uint32& ioDocNr, uint32& ioPageNr, uint32& ioDocSize);
	void			Erase(uint32 inDocNr);
	bool			Find(uint32 inDocNr, uint32& outPageNr, uint32& outDocSize);

	void			Insert(uint32 inDocNr, uint32 inPageNr, uint32 inDocSize, uint32 inIndex);
	static void		Move(M6DocStoreIndexPage& inSrc, M6DocStoreIndexPage& inDst,
						uint32 inSrcIndex, uint32 inDstIndex, uint32 inCount);

//	void			Underflow();
};

// --------------------------------------------------------------------

template<class T>
class M6DocStorePagePtr
{
  public:
	typedef T			M6DocStorePage;

						M6DocStorePagePtr();
						M6DocStorePagePtr(M6DocStoreImpl& inStoreImpl,
							M6DocStorePage* inPage);
						M6DocStorePagePtr(const M6DocStorePagePtr& inPtr);
						~M6DocStorePagePtr();

	M6DocStorePagePtr&	operator=(const M6DocStorePagePtr& inPtr);

//	M6DocStorePage*		release();
//	void				reset(M6DocStorePage* inPage);

	M6DocStorePage&		operator*() const				{ return *mPage; }
	M6DocStorePage*		operator->() const				{ return mPage; }
						operator bool() const			{ return mPage != nullptr; }

	void				swap(M6DocStorePagePtr& ptr)	{ std::swap(mPage, ptr.mPage); }

  private:
	M6DocStoreImpl*		mImpl;
	M6DocStorePage*		mPage;
};

typedef M6DocStorePagePtr<M6DocStoreDataPage>	M6DocStoreDataPagePtr;
typedef M6DocStorePagePtr<M6DocStoreIndexPage>	M6DocStoreIndexPagePtr;

// --------------------------------------------------------------------

class M6DocStoreImpl
{
  public:
					M6DocStoreImpl(const string& inPath, MOpenMode inMode);
					~M6DocStoreImpl();

	uint32			Size() const					{ return mHeader.mDocCount; }

	uint32			StoreDocument(M6Document* inDocument);
	void			EraseDocument(uint32 inDocNr);
	bool			FetchDocument(uint32 inDocNr, M6Document& outDocument);

	template<class T>
	M6DocStorePagePtr<T>	Allocate();

	template<class T>
	M6DocStorePagePtr<T>	Load(uint32 inPageNr);

	void			Reference(M6DocStorePage* inPage);
	void			Release(M6DocStorePage* inPage);

	void			Commit();
	void			Rollback();
	void			SetAutoCommit(bool inAutoCommit);

  private:

	struct M6CachedPage;
	typedef M6CachedPage*	M6CachedPagePtr;
	
	struct M6CachedPage
	{
		uint32				mPageNr;
		uint32				mRefCount;
		M6DocStorePage*		mPage;
		M6CachedPagePtr		mNext;
		M6CachedPagePtr		mPrev;
	};

	M6File					mFile;
	M6DocStoreHdr			mHeader;
	M6DocStoreIndexPagePtr	mRoot;
	bool					mDirty;
	bool					mAutoCommit;

	void			InitCache();
	M6CachedPagePtr	GetCachePage(uint32 inPageNr);
	
	M6CachedPagePtr	mCache,	mLRUHead, mLRUTail;
	uint32			mCacheCount;
};

// --------------------------------------------------------------------

template<class T>
M6DocStorePagePtr<T>::M6DocStorePagePtr()
	: mImpl(nullptr)
	, mPage(nullptr)
{
}

template<class T>
M6DocStorePagePtr<T>::M6DocStorePagePtr(
		M6DocStoreImpl& inStoreImpl, M6DocStorePage* inPage)
	: mImpl(&inStoreImpl)
	, mPage(inPage)
{
	if (mPage != nullptr)
		mImpl->Reference(mPage);
}

template<class T>
M6DocStorePagePtr<T>::M6DocStorePagePtr(const M6DocStorePagePtr& inPtr)
	: mImpl(inPtr.mImpl)
	, mPage(inPtr.mPage)
{
	if (mPage != nullptr)
		mImpl->Reference(mPage);
}

template<class T>
M6DocStorePagePtr<T>::~M6DocStorePagePtr()
{
	if (mPage != nullptr)
		mImpl->Release(mPage);	
}

template<class T>
M6DocStorePagePtr<T>& M6DocStorePagePtr<T>::operator=(const M6DocStorePagePtr& inPtr)
{
	if (this != &inPtr and mPage != inPtr.mPage)
	{
		if (mPage != nullptr)
			mImpl->Release(mPage);
		mImpl = inPtr.mImpl;
		mPage = inPtr.mPage;
		if (mPage != nullptr)
			mImpl->Reference(mPage);
	}
	
	return *this;
}

// --------------------------------------------------------------------

void M6DocStorePage::Flush(M6File& inFile)
{
	if (mDirty)
	{
		inFile.PWrite(*mData, mPageNr * kM6DataPageSize);
		mDirty = false;
	}
}

// --------------------------------------------------------------------

M6DocStoreDataPage::M6DocStoreDataPage(M6DocStoreImpl& inStore, M6DocStorePageData* inData, uint32 inPageNr)
	: M6DocStorePage(inStore, inData, inPageNr)
{
}

M6DocStoreDataPage::~M6DocStoreDataPage()
{
}

uint32 M6DocStoreDataPage::Store(uint32 inDocNr, const uint8* inData, uint32 inSize)
{
	uint32 result = 0;
	uint32 free = kM6DataPageTextSize - mData->mN;
	
	if (free > kM6DataPageTextCutOff)
	{
		uint8* dst = mData->mText + mData->mN;
		Write32(dst, inDocNr);
		
		result = inSize;
		if (result > free - 8)
			result = free - 8;
		
		Write16(dst, static_cast<uint16>(result));
		
		memcpy(dst, inData, result);
	}
	
	mDirty = true;
	
	return result;
}

uint32 M6DocStoreDataPage::Load(uint32 inDocNr, uint8* outData, uint32 inSize)
{
	// first search the document in mText
	uint32 docNr = 0;
	uint16 size;
	uint8* src = mData->mText;

	while (src < mData->mText + mData->mN)
	{
		docNr = Read32(src);
		size = Read16(src);

		if (docNr == inDocNr)
			break;

		src += size;
	}
	
	if (docNr != inDocNr)
		THROW(("Document not found!"));
	
	memcpy(outData, src, size);
	
	return size;
}

// --------------------------------------------------------------------

M6DocStoreIndexPage::M6DocStoreIndexPage(M6DocStoreImpl& inStore, M6DocStorePageData* inData, uint32 inPageNr)
	: M6DocStorePage(inStore, inData, inPageNr)
{
}

M6DocStoreIndexPage::~M6DocStoreIndexPage()
{
}

void M6DocStoreIndexPage::Insert(uint32 inDocNr, uint32 inPageNr, uint32 inDocSize, uint32 inIndex)
{
	memmove(mData->mData + inIndex + 1, mData->mData + inIndex, sizeof(M6DocStoreIndexEntry) * (mData->mN - inIndex));
	mData->mData[inIndex].mPageNr = inPageNr;
	mData->mData[inIndex].mDocSize = inDocSize;
	++mData->mN;
	mDirty = true;
}

void M6DocStoreIndexPage::Move(M6DocStoreIndexPage& inSrc, M6DocStoreIndexPage& inDst,
	uint32 inSrcIndex, uint32 inDstIndex, uint32 inCount)
{
	M6DocStorePageData& src = *inSrc.mData;
	M6DocStorePageData& dst = *inDst.mData;

	if (inDstIndex < dst.mN)
		memmove(dst.mData + inDstIndex + inCount, dst.mData + inDstIndex, (dst.mN - inDstIndex) * sizeof(M6DocStoreIndexEntry));
	
	memcpy(dst.mData + inDstIndex, src.mData + inSrcIndex, inCount * sizeof(M6DocStoreIndexEntry));
	src.mN -= inCount;
	dst.mN += inCount;
	
	if (inSrcIndex + inCount < src.mN)
		memmove(src.mData + inSrcIndex, src.mData + inSrcIndex + inCount, (src.mN - inSrcIndex) * sizeof(M6DocStoreIndexEntry));

	inSrc.mDirty = true;
	inDst.mDirty = true;
}

bool M6DocStoreIndexPage::Insert(uint32& ioDocNr, uint32& ioPageNr, uint32& ioDocSize)
{
	assert(mData->mType == eM6DocStoreIndexBranchPage or mData->mType == eM6DocStoreIndexLeafPage);

	bool result = false;
	M6DocStoreIndexEntry* data = mData->mData;
	
	int32 L = 0, R = mData->mN - 1;
	while (L <= R)
	{
		int32 i = (L + R) / 2;
		
		if (ioDocNr < data[i].mDocNr)
			R = i - 1;
		else
			L = i + 1;
	}
	
	if (mData->mType == eM6DocStoreIndexLeafPage)
	{
		int32 ix = R + 1;
		
		if (data[ix].mDocNr == ioDocNr)
		{
			data[ix].mPageNr = ioPageNr;
			data[ix].mDocSize = ioDocSize;
		}
		else if (mData->mN + 1 < kM6DataPageIndexCount)
			Insert(ioDocNr, ioPageNr, ioDocSize, ix);
		else
		{
			M6DocStoreIndexPagePtr next(mStore.Allocate<M6DocStoreIndexPage>());
			next->mData->mType = eM6DocStoreIndexLeafPage;
			int32 split = mData->mN / 2;
			
			Move(*this, *next, split, 0, mData->mN - split);

			next->SetLink(mData->mLink);
			mData->mLink = next->GetPageNr();
			
			if (ix <= mData->mN)
				Insert(ioDocNr, ioPageNr, ioDocSize, ix);
			else
				next->Insert(ioDocNr, ioPageNr, ioDocSize, ix - mData->mN);
			
			ioDocNr = next->mData->mData[0].mDocNr;
			ioPageNr = next->mData->mData[0].mPageNr;
			ioDocSize = next->mData->mData[0].mDocSize;
			
			result = true;
		}
		mDirty = true;
	}
	else	// branch page
	{
		uint32 pageNr;

		if (R < 0)
			pageNr = mData->mLink;
		else
			pageNr = data[R].mPageNr;
		
		M6DocStoreIndexPagePtr page(mStore.Load<M6DocStoreIndexPage>(pageNr));
		if (page->Insert(ioDocNr, ioPageNr, ioDocSize))
		{
			int32 ix = R + 1;
			
			if (mData->mN + 1 < kM6DataPageIndexCount)
				Insert(ioDocNr, ioPageNr, ioDocSize, ix);
			else
			{
				M6DocStoreIndexPagePtr next(mStore.Allocate<M6DocStoreIndexPage>());
				next->mData->mType = eM6DocStoreIndexBranchPage;
				int32 split = mData->mN / 2;
				
				uint32 upDocNr, downPageNr;
				
				if (ix == split)
				{
					upDocNr = ioDocNr;
					downPageNr = ioPageNr;

					Move(*this, *next, split, 0, mData->mN - split);
				}
				else
				{
					if (ix < split)
						--split;

					upDocNr = data[split].mDocNr;
					downPageNr = data[split].mPageNr;
					
					Move(*this, *next, split + 1, 0, mData->mN - split - 1);
					mData->mN -= 1;
					
					if (ioDocNr < next->mData->mData[0].mDocNr)
						Insert(ioDocNr, ioPageNr, ioDocSize, ix);
					else
						next->Insert(ioDocNr, ioPageNr, ioDocSize, ix - split - 1);
				}
				
				next->SetLink(downPageNr);
				ioDocNr = upDocNr;
				ioPageNr = next->GetPageNr();
				
				result = true;
			}
			mDirty = true;
		}
	}
	
	return result;
}

void M6DocStoreIndexPage::Erase(uint32 inDocNr)
{
}

bool M6DocStoreIndexPage::Find(uint32 inDocNr, uint32& outPageNr, uint32& outDocSize)
{
	bool result = false;
	M6DocStoreIndexEntry* data = mData->mData;
	
	int32 L = 0, R = mData->mN - 1;
	while (L <= R)
	{
		int32 i = (L + R) / 2;
		
		if (inDocNr < data[i].mDocNr)
			R = i - 1;
		else
			L = i + 1;
	}
	
	if (mData->mType == eM6DocStoreIndexLeafPage)
	{
		int32 ix = R + 1;
		
		if (data[ix].mDocNr == inDocNr)
		{
			outPageNr = data[ix].mPageNr;
			outDocSize = data[ix].mDocSize;
			result = true;
		}
	}
	else	// branch page
	{
		uint32 pageNr;

		if (R < 0)
			pageNr = mData->mLink;
		else
			pageNr = data[R].mPageNr;
		
		M6DocStoreIndexPagePtr page(mStore.Load<M6DocStoreIndexPage>(pageNr));
		return page->Find(inDocNr, outPageNr, outDocSize);
	}
	
	return result;
}

// --------------------------------------------------------------------

M6DocStoreImpl::M6DocStoreImpl(const string& inPath, MOpenMode inMode)
	: mFile(inPath, inMode)
	, mDirty(false)
	, mAutoCommit(true)
{
	InitCache();

	if (inMode == eReadWrite and mFile.Size() == 0)
	{
		uint8 data[kM6DataPageSize] = "";
		mFile.PWrite(data, sizeof(data), 0);

		memset(&mHeader, 0, sizeof(mHeader));

		mHeader.mSignature = kM6DocStoreSignature;
		mHeader.mHeaderSize = sizeof(mHeader);
		mHeader.mNextDocNumber = 1;
		mDirty = true;
		
		mFile.PWrite(mHeader, 0);
	}
	else
		mFile.PRead(mHeader, 0);
	
	assert(mHeader.mSignature == kM6DocStoreSignature);
	assert(mHeader.mHeaderSize == sizeof(mHeader));
}

M6DocStoreImpl::~M6DocStoreImpl()
{
	 if (mDirty)
	 	mFile.PWrite(mHeader, 0);
}

uint32 M6DocStoreImpl::StoreDocument(M6Document* inDocument)
{
	vector<uint8> data;
	inDocument->Compress(data);

	if (data.empty())
		THROW(("Empty document"));
	
	if (data.size() > numeric_limits<uint32>::max())
		THROW(("Document too large"));

	uint8* ptr = &data[0];
	uint32 size = static_cast<uint32>(data.size());
	uint32 pageNr = mHeader.mLastDataPage;

	M6DocStoreDataPagePtr dataPage;
	if (pageNr == 0)
	{
		dataPage = Allocate<M6DocStoreDataPage>();
		dataPage->SetPageType(eM6DocStoreDataPage);
		pageNr = dataPage->GetPageNr();
	}
	else
		dataPage = Load<M6DocStoreDataPage>(pageNr);

	uint32 docNr = mHeader.mNextDocNumber, docPageNr = pageNr, docSize = size;
	
	while (size > 0)
	{
		uint32 k = dataPage->Store(docNr, ptr, size);
		
		ptr += k;
		size -= k;
		
		if (size > 0)
		{
			M6DocStoreDataPagePtr next(Allocate<M6DocStoreDataPage>());
			next->SetPageType(eM6DocStoreDataPage);
			pageNr = next->GetPageNr();
			dataPage->SetLink(pageNr);
			mHeader.mLastDataPage = pageNr;
			dataPage = next;
			
			if (k == 0)	// can happen if last page was too full to start writing data
				docPageNr = pageNr;
		}
	}
	
	if (not mRoot)
	{
		if (mHeader.mIndexRoot == 0)
		{
			mRoot = Allocate<M6DocStoreIndexPage>();
			mRoot->SetPageType(eM6DocStoreIndexLeafPage);
			mHeader.mIndexRoot = mRoot->GetPageNr();
		}
		else
			mRoot = Load<M6DocStoreIndexPage>(mHeader.mIndexRoot);
	}
	
	if (mRoot->Insert(docNr, docPageNr, docSize))
	{
		M6DocStoreIndexPagePtr newRoot(Allocate<M6DocStoreIndexPage>());
		newRoot->SetPageType(eM6DocStoreIndexBranchPage);
		newRoot->SetLink(mHeader.mIndexRoot);
		newRoot->Insert(docNr, docPageNr, 0, 0);
		mHeader.mIndexRoot = newRoot->GetPageNr();
		
		mRoot = newRoot;
	}
	
	++mHeader.mNextDocNumber;
	mDirty = true;
	
	if (mAutoCommit)
		Commit();
	
	return docNr;
}

void M6DocStoreImpl::EraseDocument(uint32 inDocNr)
{
	THROW(("unimplemented"));
}

bool M6DocStoreImpl::FetchDocument(uint32 inDocNr, M6Document& outDocument)
{
	uint32 pageNr, size;
	bool result = false;
	
	if (mRoot->Find(inDocNr, pageNr, size))
	{
		vector<uint8> data(size);
		uint8* ptr = &data[0];
		
		while (size > 0)
		{
			M6DocStoreDataPagePtr dataPage(Load<M6DocStoreDataPage>(pageNr));
			
			uint32 read = dataPage->Load(inDocNr, ptr, size);
			ptr += read;
			size -= read;
			
			pageNr = dataPage->GetLink();
		}
		
		outDocument.Decompress(data);
		
		result = true;
	}
	
	return result;
}

template<class T>
M6DocStorePagePtr<T> M6DocStoreImpl::Allocate()
{
	int64 fileSize = mFile.Size();
	uint32 pageNr = static_cast<uint32>((fileSize - 1) / kM6DataPageSize + 1ULL);
	int64 offset = pageNr * kM6DataPageSize;
	mFile.Truncate(offset + kM6DataPageSize);
	
	M6DocStorePageData* data = new M6DocStorePageData;
	memset(data, 0, kM6DataPageSize);
	
	M6CachedPagePtr cp = GetCachePage(pageNr);
	cp->mPage = new T(*this, data, pageNr);
	
	return M6DocStorePagePtr<T>(*this, static_cast<T*>(cp->mPage));
}

template<class T>
M6DocStorePagePtr<T> M6DocStoreImpl::Load(uint32 inPageNr)
{
	if (inPageNr == 0)
		THROW(("Invalid page number"));

	M6CachedPagePtr cp = mLRUHead;
	while (cp != nullptr and cp->mPageNr != inPageNr)
		cp = cp->mNext; 

	if (cp == nullptr)
	{
		cp = GetCachePage(inPageNr);
		
		M6DocStorePageData* data = new M6DocStorePageData;
		mFile.PRead(*data, inPageNr * kM6DataPageSize);

		switch (data->mType)
		{
			case eM6DocStoreDataPage:
				cp->mPage = new M6DocStoreDataPage(*this, data, inPageNr);
				break;
			
			case eM6DocStoreIndexLeafPage:
			case eM6DocStoreIndexBranchPage:
				cp->mPage = new M6DocStoreIndexPage(*this, data, inPageNr);
				break;
			
			default:
				delete data;
				THROW(("Invalid page type in document store"));
				break;
		}
	}
	
	return M6DocStorePagePtr<T>(*this, static_cast<T*>(cp->mPage));
}

void M6DocStoreImpl::InitCache()
{
	const uint32 kM6CacheCount = 16;
	
	mCacheCount = kM6CacheCount;
	mCache = new M6CachedPage[mCacheCount];
	
	for (uint32 ix = 0; ix < mCacheCount; ++ix)
	{
		mCache[ix].mPageNr = 0;
		mCache[ix].mRefCount = 0;
		mCache[ix].mPage = nullptr;
		mCache[ix].mNext = mCache + ix + 1;
		mCache[ix].mPrev = mCache + ix - 1;
	}
	
	mCache[0].mPrev = mCache[mCacheCount - 1].mNext = nullptr;
	mLRUHead = mCache;
	mLRUTail = mCache + mCacheCount - 1;
}

M6DocStoreImpl::M6CachedPagePtr M6DocStoreImpl::GetCachePage(uint32 inPageNr)
{
	M6CachedPagePtr result = mLRUTail;
	
	// now search backwards for a cached page that can be recycled
	uint32 n = 0;
	while (result != nullptr and result->mPage != nullptr and 
		(result->mRefCount > 0 or (result->mPage->IsDirty() and mAutoCommit == false)))
	{
		result = result->mPrev;
		++n;
	}
	
	if (result == nullptr)
		THROW(("cache full"));

//	// we could end up with a full cache, if so, double the cache
//	
//	if (result == nullptr)
//	{
//		mCacheCount *= 2;
//		M6CachedPagePtr tmp = new M6CachedPage[mCacheCount];
//		
//		for (uint32 ix = 0; ix < mCacheCount; ++ix)
//		{
//			tmp[ix].mPageNr = 0;
//			tmp[ix].mNext = tmp + ix + 1;
//			tmp[ix].mPrev = tmp + ix - 1;
//		}
//
//		tmp[0].mPrev = tmp[mCacheCount - 1].mNext = nullptr;
//
//		for (M6CachedPagePtr a = mLRUHead, b = tmp; a != nullptr; a = a->mNext, b = b->mNext)
//		{
//			b->mPageNr = a->mPageNr;
//			b->mPage = a->mPage;
//		}
//		
//		delete[] mCache;
//		mCache = tmp;
//		
//		mLRUHead = mCache;
//		mLRUTail = mCache + mCacheCount - 1;
//		
//		result = mLRUTail;
//	}

	if (result == mLRUTail or (result != mLRUHead and n > mCacheCount / 4))
	{
		if (result == mLRUTail)
			mLRUTail = result->mPrev;
		
		if (result->mPrev)
			result->mPrev->mNext = result->mNext;
		if (result->mNext)
			result->mNext->mPrev = result->mPrev;
		
		result->mPrev = nullptr;
		result->mNext = mLRUHead;
		mLRUHead->mPrev = result;
		mLRUHead = result;
	}
	
	if (result->mPage != nullptr and result->mPage->IsDirty())
		result->mPage->Flush(mFile);

	result->mPageNr = inPageNr;
	delete result->mPage;
	result->mPage = nullptr;
	result->mRefCount = 0;
//	result->mDirty = false;
	
	return result;
}

void M6DocStoreImpl::Reference(M6DocStorePage* inPage)
{
	if (inPage == nullptr)
		THROW(("Invalid page number"));

	M6CachedPagePtr cp = mLRUHead;
	while (cp != nullptr and cp->mPage != inPage)
		cp = cp->mNext; 

	if (cp == nullptr)
		THROW(("page not found in cache"));
	
	cp->mRefCount += 1;
}

void M6DocStoreImpl::Release(M6DocStorePage* inPage)
{
	if (inPage == nullptr)
		THROW(("Invalid page number"));

	M6CachedPagePtr cp = mLRUHead;
	while (cp != nullptr and cp->mPage != inPage)
		cp = cp->mNext; 

	if (cp == nullptr)
		THROW(("page not found in cache"));
	
	cp->mRefCount -= 1;
}

void M6DocStoreImpl::Commit()
{
	for (uint32 ix = 0; ix < mCacheCount; ++ix)
	{
		if (mCache[ix].mPage != nullptr and mCache[ix].mPage->IsDirty())
			mCache[ix].mPage->Flush(mFile);
	}
}

void M6DocStoreImpl::Rollback()
{
	for (uint32 ix = 0; ix < mCacheCount; ++ix)
	{
		if (mCache[ix].mPage and mCache[ix].mPage->IsDirty())
		{
			assert(mCache[ix].mRefCount == 0);

			mCache[ix].mPage->SetDirty(false);
			delete mCache[ix].mPage;
			mCache[ix].mPage = nullptr;
			mCache[ix].mPageNr = 0;
			mCache[ix].mRefCount = 0;
		}
	}
}

void M6DocStoreImpl::SetAutoCommit(bool inAutoCommit)
{
	mAutoCommit = inAutoCommit;
}

// --------------------------------------------------------------------

M6DocStore::M6DocStore(const string& inPage, MOpenMode inMode)
	: mImpl(new M6DocStoreImpl(inPage, inMode))
{
}

M6DocStore::~M6DocStore()
{
	delete mImpl;
}

uint32 M6DocStore::StoreDocument(M6Document* inDocument)
{
	return mImpl->StoreDocument(inDocument);
}

void M6DocStore::EraseDocument(uint32 inDocNr)
{
	mImpl->EraseDocument(inDocNr);
}

bool M6DocStore::FetchDocument(uint32 inDocNr, M6Document& outDocument)
{
	return mImpl->FetchDocument(inDocNr, outDocument);
}

uint32 M6DocStore::size() const
{
	return mImpl->Size();
}
