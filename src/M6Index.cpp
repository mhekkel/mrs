// Code to store a B+ index with variable key length and various values.
//
//	TODO: - implement a way to recover deallocated pages
//

#include "M6Lib.h"

#include <deque>
#include <list>
#include <vector>
#include <numeric>

#include <boost/static_assert.hpp>

#include "M6Index.h"
#include "M6Error.h"

using namespace std;

// --------------------------------------------------------------------

// The index will probably never have keys less than 3 bytes in length.
// Including the length byte, this means a minimal key length of 4. Add
// a data element of uint32 and the minimal storage per entry is 8 bytes.
// Given this, the maximum number of keys we will ever store in a page
// is (pageSize - headerSize) / 8. For a 8192 byte page and 8 byte header
// this boils down to 1023.

enum M6IndexPageType : uint8
{
	eM6IndexEmptyPage			= 'e',
	eM6IndexBranchPage			= 'b',
	eM6IndexSimpleLeafPage		= 'l',
	eM6IndexMultiLeafPage		= 'm',
	eM6IndexMultiIDLLeafPage	= 'i'
};

struct M6IndexPageHeader
{
	M6IndexPageType	mType;
	uint8			mFlags;
	uint16			mN;
	uint32			mP0;
};

const uint32
	kM6IndexPageSize		= 8192,
	kM6IndexPageHeaderSize	= sizeof(M6IndexPageHeader),
	kM6KeySpace				= kM6IndexPageSize - kM6IndexPageHeaderSize,
	kM6MinKeySpace			= kM6KeySpace / 2,
	kM6MaxEntriesPerPage	= kM6KeySpace / 8;	// see above

template<M6IndexPageType>
struct M6IndexPageDataTraits {};

template<>
struct M6IndexPageDataTraits<eM6IndexBranchPage>
{
	typedef uint32 M6DataElement;
};

template<>
struct M6IndexPageDataTraits<eM6IndexSimpleLeafPage>
{
	typedef uint32 M6DataElement;
};

template<>
struct M6IndexPageDataTraits<eM6IndexMultiLeafPage>
{
	struct M6DataElement
	{
		uint32	mCount;
		int64	mBitVector;
	};
};

template<>
struct M6IndexPageDataTraits<eM6IndexMultiIDLLeafPage>
{
	struct M6DataElement
	{
		uint32	mCount;
		int64	mBitVector;
		int64	mIDLOffset;
	};
};

template<M6IndexPageType>
struct M6IndexPageDataT
{
	typedef typename M6IndexPageDataTraits<eM6IndexBranchPage>	traits;
	typedef traits::M6DataElement								M6DataType;
	
	enum { kM6DataCount	= kM6KeySpace / sizeof(M6DataType) };
	
	union
	{
		uint8		mKeys[kM6KeySpace];
		M6DataType	mData[kM6DataCount];
	};
};

typedef M6IndexPageDataT<eM6IndexBranchPage>		M6IndexBranchPageData;
typedef M6IndexPageDataT<eM6IndexSimpleLeafPage>	M6IndexSimpleLeafPageData;
typedef M6IndexPageDataT<eM6IndexMultiLeafPage>		M6IndexMultiLeafPageData;
typedef M6IndexPageDataT<eM6IndexMultiIDLLeafPage>	M6IndexMultiIDLLeafPageData;

struct M6IndexPageData : public M6IndexPageHeader
{
	template<M6IndexPageType T>
	M6IndexPageDataT<T>& GetData();
	
	union
	{
		M6IndexBranchPageData		branch;
		M6IndexSimpleLeafPageData	leaf;
		M6IndexMultiLeafPageData	multi_leaf;
		M6IndexMultiIDLLeafPageData	idl_leaf;
	};


};

template<> M6IndexPageDataT<eM6IndexBranchPage>& M6IndexPageData::GetData() { return branch; }
template<> M6IndexPageDataT<eM6IndexSimpleLeafPage>& M6IndexPageData::GetData() { return leaf; }
template<> M6IndexPageDataT<eM6IndexMultiLeafPage>& M6IndexPageData::GetData() { return multi_leaf; }
template<> M6IndexPageDataT<eM6IndexMultiIDLLeafPage>& M6IndexPageData::GetData() { return idl_leaf; }

BOOST_STATIC_ASSERT(sizeof(M6IndexPageData) == kM6IndexPageSize);

const uint32
//	kM6IndexFileSignature	= FOUR_CHAR_INLINE('m6ix');
	kM6IndexFileSignature	= 'm6ix';

struct M6IxFileHeader
{
	uint32		mSignature;
	uint32		mHeaderSize;
	uint32		mSize;
	uint32		mDepth;
	uint32		mRoot;

	template<class Archive>
	void serialize(Archive& ar)
	{
		ar & mSignature & mHeaderSize & mSize & mRoot & mDepth;
	}
};

union M6IxFileHeaderPage
{
	M6IxFileHeader	mHeader;
	uint8			mFiller[kM6IndexPageSize];
};

BOOST_STATIC_ASSERT(sizeof(M6IxFileHeaderPage) == kM6IndexPageSize);

// --------------------------------------------------------------------

template<class M6DataType>
class M6IndexPage;

template<class M6DataType>
class M6IndexBranchPage;

template<class M6DataType>
class M6IndexPagePtr
{
  public:
					M6IndexPagePtr();
					M6IndexPagePtr(M6IndexPage<M6DataType>* inPage);
					M6IndexPagePtr(const M6IndexPagePtr& inPtr);
					~M6IndexPagePtr();

	M6IndexPagePtr&	operator=(const M6IndexPagePtr& inPtr);

	M6IndexPage<M6DataType>*
					release();
	void			reset(M6IndexPage<M6DataType>* inPage);

	M6IndexPage&	operator*() const			{ return *mPage; }
	M6IndexPage*	operator->() const			{ return mPage; }
					operator bool() const		{ return mPage != nullptr; }

	void			swap(M6IndexPagePtr& ptr)	{ std::swap(mPage, ptr.mPage); }

  private:
	M6IndexPage<M6DataType>*	mPage;	
};

// --------------------------------------------------------------------

struct M6IndexImpl {};

template<class M6DataType>
class M6IndexImplT : public M6IndexImpl
{
  public:
	typedef typename M6BasicIndex::iterator	iterator;

					M6IndexImplT(M6BasicIndex& inIndex, const string& inPath, MOpenMode inMode);
					M6IndexImplT(M6BasicIndex& inIndex, const string& inPath,
						M6SortedInputIterator& inData);
					~M6IndexImplT();

	void			Insert(const string& inKey, const M6DataType& inValue);
	bool			Erase(const string& inKey);
	bool			Find(const string& inKey, M6DataType& outValue);
	void			Vacuum();
	
	iterator		Begin();
	iterator		End();

	uint32			Size() const				{ return mHeader.mSize; }
	uint32			Depth() const				{ return mHeader.mDepth; }
	
	int				CompareKeys(const char* inKeyA, size_t inKeyLengthA,
						const char* inKeyB, size_t inKeyLengthB) const
					{
						return mIndex.CompareKeys(inKeyA, inKeyLengthA, inKeyB, inKeyLengthB);
					}

	int				CompareKeys(const string& inKeyA, const string& inKeyB) const
					{
						return mIndex.CompareKeys(inKeyA.c_str(), inKeyA.length(), inKeyB.c_str(), inKeyB.length());
					}

#if DEBUG
	void			Validate();
	void			Dump();
#endif

	void			Commit();
	void			Rollback();
	void			SetAutoCommit(bool inAutoCommit);

	// page cache
	template<class P>
	M6IndexPagePtr	Allocate();
	M6IndexPagePtr	Load(uint32 inPageNr);
	
  private:

	void			CreateUpLevels(deque<pair<string,M6DataType>>& up);

	M6File			mFile;
	M6BasicIndex&	mIndex;
	M6IxFileHeader	mHeader;
#pragma message("TODO: mRoot gaan gebruiken")
	M6IndexPagePtr	mRoot;
	bool			mDirty;
	bool			mAutoCommit;

	struct M6CachedPage;
	typedef M6CachedPage*	M6CachedPagePtr;

	struct M6CachedPage
	{
#pragma message("TODO: Uitbreiden met dirty, refcount")

		uint32			mPageNr;
		M6IndexPagePtr	mPage;
		M6CachedPagePtr	mNext;
		M6CachedPagePtr	mPrev;
	};

	void			InitCache();
	M6CachedPagePtr	GetCachePage(uint32 inPageNr);
	
	M6CachedPagePtr	mCache,	mLRUHead, mLRUTail;
	uint32			mCacheCount;
	const static uint32
					kM6CacheCount = 16;
};

// --------------------------------------------------------------------

template<class M6DataType>
class M6IndexPage
{
  public:
	virtual 		~M6IndexPage();

	void			Deallocate();
	void			Flush(M6File& inFile);

	void			Reference()						{ ++mRefCount; }
	void			Release();
	int32			GetRefCount() const				{ return mRefCount; }
	
	uint32			GetPageNr() const				{ return mPageNr; }
	void			MoveTo(uint32 inPageNr);

	virtual bool	IsLeaf() const					{ return true; }
	
	bool			IsDirty() const					{ return mDirty; }
	void			SetDirty(bool inDirty)			{ mDirty = inDirty; }
	
	uint32			GetN() const					{ return mData->mN; }
	virtual uint32	Free() const					{ return kM6KeySpace - mKeyOffsets[mData->mN] - mData->mN * sizeof(M6DataType); }
	virtual bool	CanStore(const string& inKey) const
					{
						return
							static_cast<uint32>(mData->mN) < kM6MaxEntriesPerPage and
							Free() >= inKey.length() + 1 + sizeof(M6DataType);
					}
	
	bool			TooSmall() const				{ return Free() > kM6MinKeySpace; }
	
	virtual bool	Insert(string& ioKey, const M6DataType& inValue, uint32& outLink, M6IndexBranchPage* inParent) = 0;
	virtual bool	Erase(string& ioKey, int32 inIndex, M6IndexBranchPage* inParent, M6IndexBranchPage* inLinkPage, uint32 inLinkIndex) = 0;

#if DEBUG
	void			Validate(const string& inKey, M6IndexBranchPage* inParent);
	void			Dump(int inLevel, M6IndexBranchPage* inParent);
#endif

  protected:
					M6IndexPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr);

	M6IndexImpl&	mIndexImpl;
	M6IndexPageData* mData;
	uint32			mPageNr;
	int32			mRefCount;
	bool			mLocked;
	bool			mDirty;
	uint16			mKeyOffsets[kM6MaxEntriesPerPage + 1];

  private:

					M6IndexPage(const M6IndexPage&);
	M6IndexPage&	operator=(const M6IndexPage&);
};

// --------------------------------------------------------------------

template<class M6DataType>
class M6IndexBranchPage : public M6IndexPage<M6DataType>
{
  public:
					M6IndexBranchPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr);

	virtual bool	Find(const string& inKey, M6DataType& outValue);
	virtual bool	Insert(string& ioKey, const M6DataType& inValue, uint32& outLink, M6IndexBranchPage* inParent);
	virtual bool	Erase(string& ioKey, int32 inIndex, M6IndexBranchPage* inParent, M6IndexBranchPage* inLinkPage, uint32 inLinkIndex);

	void			ReplaceKey(uint32 inIndex, const string& inKey);

  protected:

	void			BinarySearch(const string& inKey, int32& outIndex, bool& outMatch) const;

	string			GetKey(uint32 inIndex) const;
	uint32			GetValue(uint32 inIndex) const;
	void			SetValue(uint32 inIndex, uint32 inValue);

	void			GetKeyValue(uint32 inIndex, string& outKey, uint32 outValue) const;
	void			InsertKeyValue(const string& inKey, uint32 inValue, uint32 inIndex);

	void			EraseEntry(uint32 inIndex);

	virtual bool	Underflow(M6IndexPage& inRight, uint32 inIndex, M6IndexBranchPage* inParent) = 0;

	static void		MoveEntries(M6IndexPage& inFrom, M6IndexPage& inTo,
						uint32 inFromOffset, uint32 inToOffset, uint32 inCount);

	M6IndexBranchPageData&
					mPageData;
};

// --------------------------------------------------------------------

template<class M6DataType>
M6IndexBranchPage<M6DataType>::M6IndexBranchPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
	: M6IndexPage(inIndexImpl, inData, inPageNr)
	, mPageData(inData->GetData())
{
	mData->mType = eM6IndexBranchPage;
}

template<class M6DataType>
bool M6IndexBranchPage<M6DataType>::Find(const string& inKey, M6DataType& outValue)
{
	bool match;
	int32 ix;
	
	BinarySearch(inKey, ix, match);

	uint32 pageNr;
	
	if (ix < 0)
		pageNr = mData->mLink;
	else
		pageNr = GetValue(ix);
	
	M6IndexPagePtr<M6DataType> page(mIndexImpl.Load(pageNr));
	return page->Find(inKey, outValue);
}

/*
	Insert returns a bool indicating the depth increased.
	In that case the ioKey and ioValue are updated to the values
	to be inserted in the calling page (or a new root has to be made).
*/

template<class M6DataType>
bool M6IndexBranchPage<M6DataType>::Insert(string& ioKey, const M6DataType& inValue, uint32& outLink, M6IndexBranchPage* inParent)
{
	bool result = false, match;
	int32 ix;

	BinarySearch(ioKey, ix, match);
	
	uint32 pageNr;
	
	if (ix < 0)
		pageNr = mData->mLink;
	else
		pageNr = GetValue(ix);

	M6IndexPagePtr<M6DataType> page(mIndexImpl.Load(pageNr));
	
	uint32 link;
	if (page->Insert(ioKey, inValue, link, this))
	{
		// page was split, we now need to store ioKey in our page
		ix += 1;	// we need to insert at ix + 1

		if (CanStore(ioKey))
			InsertKeyValue(ioKey, link, ix);
		else
		{
			// the key needs to be inserted but it didn't fit
			// so we need to split the page

			M6IndexPagePtr next(mIndexImpl.Allocate<M6IndexBranchPage>());
			
			int32 split = mData->mN / 2;
			string upKey;
			uint32 downPage;

			if (ix == split)
			{
				upKey = ioKey;
				downPage = link;

				MoveEntries(*this, *next, split, 0, mData->mN - split);
			}
			else if (ix < split)
			{
				--split;
				upKey = GetKey(split);
				downPage = GetValue(split);

				MoveEntries(*this, *next, split + 1, 0, mData->mN - split - 1);
				mData->mN -= 1;

				if (ix <= split)
					InsertKeyValue(ioKey, link, ix);
				else
					next->InsertKeyValue(ioKey, link, ix - split - 1);
			}
			else
			{
				upKey = GetKey(split);
				downPage = GetValue(split);

				MoveEntries(*this, *next, split + 1, 0, mData->mN - split - 1);
				mData->mN -= 1;

				if (ix < split)
					InsertKeyValue(ioKey, link, ix);
				else
					next->InsertKeyValue(ioKey, link, ix - split - 1);
			}

			next->SetLink(downPage);
			
			ioKey = upKey;
			outLink = next->GetPageNr();
			
			result = true;
		}
	}

//	assert(mData->mN >= kM6MinEntriesPerPage or inParent == nullptr);
	assert(mData->mN <= kM6MaxEntriesPerPage);

	return result;
}

bool M6IndexBranchPage::Erase(string& ioKey, int32 inIndex, M6IndexBranchPage* inParent, M6IndexBranchPage* inLinkPage, uint32 inLinkIndex)
{
	bool result = false, match = false;
	int32 ix;
	
	BinarySearch(ioKey, ix, match);
	
	assert(match == false or inLinkPage == nullptr);
	if (match)
	{
		inLinkPage = this;
		inLinkIndex = ix;
	}

	uint32 pageNr;
	
	if (ix < 0)
		pageNr = mData->mLink;
	else
		pageNr = GetValue(ix);
	
	M6IndexPagePtr page(mIndexImpl.Load(pageNr));
	if (page->Erase(ioKey, ix, this, inLinkPage, inLinkIndex))
	{
		result = true;

		if (TooSmall() and inParent != nullptr)
		{
			if (inIndex + 1 < static_cast<int32>(inParent->GetN()))
			{
				// try to compensate using our right sibling
				M6IndexPagePtr right(mIndexImpl.Load(inParent->GetValue(inIndex + 1)));
				Underflow(*right, inIndex + 1, inParent);
			}
			
			if (TooSmall() and inIndex >= 0)
			{
				// if still too small, try with the left sibling
				M6IndexPagePtr left(mIndexImpl.Load(inIndex > 0 ? inParent->GetValue(inIndex - 1) : inParent->GetLink()));
				left->Underflow(*this, inIndex, inParent);
			}
		}
	}

	return result;
}

bool M6IndexBranchPage::Underflow(M6IndexPage& inRight, uint32 inIndex, M6IndexBranchPage* inParent)
{
	M6IndexBranchPage& right(static_cast<M6IndexBranchPage&>(inRight));

	// This page left of inRight contains too few entries, see if we can fix this
	// first try a merge

	// pKey is the key in inParent at inIndex (and, since this a leaf, the first key in inRight)
	string pKey = inParent->GetKey(inIndex);
	int32 pKeyLen = static_cast<int32>(pKey.length());

	if (Free() + inRight.Free() - pKeyLen - sizeof(int64) >= kM6IndexPageKeySpace and
		mData->mN + right.mData->mN + 1 <= kM6MaxEntriesPerPage)
	{
		InsertKeyValue(pKey, inRight.GetLink(), mData->mN);
		
		// join the pages
		MoveEntries(right, *this, 0, mData->mN, right.mData->mN);
	
		inParent->EraseEntry(inIndex);
		inRight.Deallocate();
	}
	else		// redistribute the data
	{
		if (Free() > inRight.Free() and mData->mN < kM6IndexPageDataCount)	// rotate an entry from right to left
		{									// but only if it fits in the parent
			string rKey = inRight.GetKey(0);
			int32 delta = static_cast<int32>(rKey.length() - pKey.length());
			if (delta <= static_cast<int32>(inParent->Free()))
			{
				InsertKeyValue(pKey, right.mData->mLink, mData->mN);
				inParent->ReplaceKey(inIndex, rKey);
				inParent->SetValue(inIndex, right.mPageNr);
				right.mData->mLink = inRight.GetValue(0);
				inRight.EraseEntry(0);
			}
		}
		else if (right.Free() > Free() and right.mData->mN < kM6IndexPageDataCount)
		{
			string lKey = GetKey(mData->mN - 1);
			int32 delta = static_cast<int32>(lKey.length() - pKey.length());
			if (delta <= static_cast<int32>(inParent->Free()))
			{
				inRight.InsertKeyValue(pKey, right.mData->mLink, 0);
				right.mData->mLink = GetValue(mData->mN - 1);
				inParent->ReplaceKey(inIndex, lKey);
				EraseEntry(mData->mN - 1);
			}
		}
	}
	
	return not (TooSmall() or inRight.TooSmall());
}

// --------------------------------------------------------------------

template<M6IndexPageType T>
class M6IndexLeafPage : public M6IndexPage<typename M6IndexPageDataT<T>::M6DataType>
{
  public:
	typedef typename M6IndexPageDataT<T>::M6DataType		M6DataType;
	typedef M6IndexBranchPage<M6DataType>					M6IndexBranchPage;
	typedef M6IndexPageDataT<T>								M6DataPageType;

	void			SetLink(uint32 inLink);
	uint32			GetLink() const					{ return mData->mP0; }

	virtual bool	Find(const string& inKey, M6DataType& outValue);
	virtual bool	Insert(string& ioKey, M6DataType& ioValue, M6IndexBranchPage* inParent);
	virtual bool	Erase(string& ioKey, int32 inIndex, M6IndexBranchPage* inParent, M6IndexBranchPage* inLinkPage, uint32 inLinkIndex);

	virtual bool	Underflow(M6IndexPage& inRight, uint32 inIndex, M6IndexBranchPage* inParent);

	static void		MoveEntries(M6IndexPage& inFrom, M6IndexPage& inTo,
						uint32 inFromOffset, uint32 inToOffset, uint32 inCount);

  protected:
					M6IndexLeafPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
						: M6IndexPage(inIndexImpl, inData, inPageNr)
						, mPageData(inData->GetData()) {}

	M6DataPageType&	mPageData;
};

// --------------------------------------------------------------------

template<class M6DataType>
inline M6IndexPagePtr<M6DataType>::M6IndexPagePtr()
	: mPage(nullptr)
{
}

template<class M6DataType>
inline M6IndexPagePtr<M6DataType>::M6IndexPagePtr(M6IndexPage<M6DataType>* inPage)
	: mPage(inPage)
{
	if (mPage != nullptr)
		mPage->Reference();
}

template<class M6DataType>
inline M6IndexPagePtr<M6DataType>::M6IndexPagePtr(const M6IndexPagePtr& inPtr)
	: mPage(inPtr.mPage)
{
	if (mPage != nullptr)
		mPage->Reference();
}

template<class M6DataType>
inline M6IndexPagePtr<M6DataType>::~M6IndexPagePtr()
{
	if (mPage != nullptr)
		mPage->Release();
}

template<class M6DataType>
inline M6IndexPagePtr<M6DataType>& M6IndexPagePtr<M6DataType>::operator=(const M6IndexPagePtr& inPtr)
{
	if (this != &inPtr and mPage != inPtr.mPage)
	{
		if (mPage != nullptr)
			mPage->Release();

		mPage = inPtr.mPage;

		if (mPage != nullptr)
			mPage->Reference();
	}
	
	return *this;
}

template<class M6DataType>
inline M6IndexPage<M6DataType>* M6IndexPagePtr<M6DataType>::release()
{
	M6IndexPage<M6DataType>* result = mPage;
	
	if (mPage != nullptr)
		mPage->Release();
	
	mPage = nullptr;
	return result;
}

template<class M6DataType>
inline void M6IndexPagePtr<M6DataType>::reset(M6IndexPage<M6DataType>* inPage)
{
	if (mPage != nullptr)
		mPage->Release();
	mPage = inPage;
	if (mPage != nullptr)
		mPage->Reference();
}

namespace std
{
	template<class M6DataType>
	void swap(M6IndexPagePtr<M6DataType>& a, M6IndexPagePtr<M6DataType>& b)
	{
		a.swap(b);
	}	
}

// --------------------------------------------------------------------

template<class M6DataType>
M6IndexPage<M6DataType>::M6IndexPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
	: mIndexImpl(inIndexImpl)
	, mPageNr(inPageNr)
	, mRefCount(0)
	, mData(inData)
	, mLocked(false)
	, mDirty(false)
{
//	assert(mData->mN <= kM6MaxEntriesPerPage);
//	
//	uint8* key = mData->mKeys;
//	for (uint32 i = 0; i <= mData->mN; ++i)
//	{
//		assert(key <= mData->mKeys + kM6IndexPageSize);
//		assert(i <= kM6MaxEntriesPerPage);
//		mKeyOffsets[i] = static_cast<uint16>(key - mData->mKeys);
//		key += *key + 1;
//	}
}

template<class M6DataType>
M6IndexPage<M6DataType>::~M6IndexPage()
{
	assert(mRefCount == 0);
	assert(not mDirty);
	delete mData;
}

template<class M6DataType>
void M6IndexPage<M6DataType>::Release()
{
	if (--mRefCount == 0)
		delete this;
}

template<class M6DataType>
void M6IndexPage<M6DataType>::Flush(M6File& inFile)
{
	if (mDirty)
	{
		inFile.PWrite(mData, kM6IndexPageSize, mPageNr * kM6IndexPageSize);
		mDirty = false;
	}
}

template<class M6DataType>
void M6IndexPage<M6DataType>::Deallocate()
{
	mData->mType = eM6IndexEmptyPage;
	mDirty = true;
}

// move entries (keys and data) taking into account insertions and such
template<class M6DataType>
void M6IndexPage<M6DataType>::MoveEntries(M6IndexPage& inSrc, M6IndexPage& inDst,
	uint32 inSrcOffset, uint32 inDstOffset, uint32 inCount)
{
	assert(inSrcOffset <= inSrc.mData->mN);
	assert(inDstOffset <= inDst.mData->mN);
	assert(inDstOffset + inCount <= kM6MaxEntriesPerPage);
	
	// make room in dst first
	if (inDstOffset < inDst.mData->mN)
	{
		// make room in dst by shifting entries
		void* src = inDst.mData->mKeys + inDst.mKeyOffsets[inDstOffset];
		void* dst = inDst.mData->mKeys + inDst.mKeyOffsets[inDstOffset] +
			inSrc.mKeyOffsets[inSrcOffset + inCount] - inSrc.mKeyOffsets[inSrcOffset];
		uint32 n = inDst.mKeyOffsets[inDst.mData->mN] - inDst.mKeyOffsets[inDstOffset];
		memmove(dst, src, n);
		
		src = inDst.mData->mData + kM6IndexPageDataCount - inDst.mData->mN;
		dst = inDst.mData->mData + kM6IndexPageDataCount - inDst.mData->mN - inCount;
		memmove(dst, src, (inDst.mData->mN - inDstOffset) * sizeof(M6DataType));
	}
	
	// copy keys
	void* src = inSrc.mData->mKeys + inSrc.mKeyOffsets[inSrcOffset];
	void* dst = inDst.mData->mKeys + inDst.mKeyOffsets[inDstOffset];
	
	uint32 byteCount = inSrc.mKeyOffsets[inSrcOffset + inCount] -
					   inSrc.mKeyOffsets[inSrcOffset];

	assert(inSrc.mKeyOffsets[inSrcOffset] + byteCount <= kM6IndexPageKeySpace);
	assert(byteCount + inCount * sizeof(M6DataType) <= inDst.Free());

	memcpy(dst, src, byteCount);
	
	// and data	
	src = inSrc.mData->mData + kM6IndexPageDataCount - inSrcOffset - inCount;
	dst = inDst.mData->mData + kM6IndexPageDataCount - inDstOffset - inCount;
	byteCount = inCount * sizeof(M6DataType);
	memcpy(dst, src, byteCount);
	
	// and finally move remaining data in src
	if (inSrcOffset + inCount < inSrc.mData->mN)
	{
		void* src = inSrc.mData->mKeys + inSrc.mKeyOffsets[inSrcOffset + inCount];
		void* dst = inSrc.mData->mKeys + inSrc.mKeyOffsets[inSrcOffset];
		uint32 n = inSrc.mKeyOffsets[inSrc.mData->mN] - inSrc.mKeyOffsets[inSrcOffset + inCount];
		memmove(dst, src, n);
		
		src = inSrc.mData->mData + kM6IndexPageDataCount - inSrc.mData->mN;
		dst = inSrc.mData->mData + kM6IndexPageDataCount - inSrc.mData->mN + inCount;
		memmove(dst, src, (inSrc.mData->mN - inSrcOffset - inCount) * sizeof(M6DataType));
	}
	
	inDst.mData->mN += inCount;
	inSrc.mData->mN -= inCount;
	
	// update key offsets
	uint8* key = inSrc.mData->mKeys + inSrc.mKeyOffsets[inSrcOffset];
	for (int32 i = inSrcOffset; i <= inSrc.mData->mN; ++i)
	{
		assert(key <= inSrc.mData->mKeys + kM6IndexPageSize);
		inSrc.mKeyOffsets[i] = static_cast<uint16>(key - inSrc.mData->mKeys);
		key += *key + 1;
	}

	key = inDst.mData->mKeys + inDst.mKeyOffsets[inDstOffset];
	for (int32 i = inDstOffset; i <= inDst.mData->mN; ++i)
	{
		assert(key <= inDst.mData->mKeys + kM6IndexPageSize);
		inDst.mKeyOffsets[i] = static_cast<uint16>(key - inDst.mData->mKeys);
		key += *key + 1;
	}

	inSrc.mDirty = true;
	inDst.mDirty = true;
}

void M6IndexPage::SetLink(uint32 inLink)
{
	mData->mLink = inLink;
	mDirty = true;
}

void M6IndexPage::MoveTo(uint32 inPageNr)
{
	if (inPageNr != mPageNr)
	{
		M6IndexPagePtr page(mIndexImpl.Load(inPageNr));

		page->mPageNr = mPageNr;
		if (page->IsLeaf())	// only save page if it is a leaf
			page->mDirty = true;
		
		mPageNr = inPageNr;
		mDirty = true;
	}
}

inline string M6IndexPage::GetKey(uint32 inIndex) const
{
	assert(inIndex < mData->mN);
	const uint8* key = mData->mKeys + mKeyOffsets[inIndex];
	return string(reinterpret_cast<const char*>(key) + 1, *key);
}

inline int64 M6IndexPage::GetValue(uint32 inIndex) const
{
	assert(inIndex < mData->mN);
	return swap_bytes(mData->mData[kM6IndexPageDataCount - inIndex - 1]);
}

inline void M6IndexPage::SetValue(uint32 inIndex, int64 inValue)
{
	assert(inIndex < mData->mN);
	mData->mData[kM6IndexPageDataCount - inIndex - 1] = swap_bytes(inValue);
}

inline void M6IndexPage::GetKeyValue(uint32 inIndex, string& outKey, int64& outValue) const
{
	outKey = GetKey(inIndex);
	outValue = GetValue(inIndex);
}

void M6IndexPage::InsertKeyValue(const string& inKey, int64 inValue, uint32 inIndex)
{
	assert(inIndex <= mData->mN);
	
	if (inIndex < mData->mN)
	{
		void* src = mData->mKeys + mKeyOffsets[inIndex];
		void* dst = mData->mKeys + mKeyOffsets[inIndex] + inKey.length() + 1;
		
		// shift keys
		memmove(dst, src, mKeyOffsets[mData->mN] - mKeyOffsets[inIndex]);
		
		// shift data
		src = mData->mData + kM6IndexPageDataCount - mData->mN;
		dst = mData->mData + kM6IndexPageDataCount - mData->mN - 1;
		
		memmove(dst, src, (mData->mN - inIndex) * sizeof(int64));
	}
	
	uint8* k = mData->mKeys + mKeyOffsets[inIndex];
	*k = static_cast<uint8>(inKey.length());
	memcpy(k + 1, inKey.c_str(), *k);
	mData->mData[kM6IndexPageDataCount - inIndex - 1] = swap_bytes(inValue);
	++mData->mN;

	assert(mData->mN <= kM6MaxEntriesPerPage);

	// update key offsets
	for (uint32 i = inIndex + 1; i <= mData->mN; ++i)
		mKeyOffsets[i] = static_cast<uint16>(mKeyOffsets[i - 1] + mData->mKeys[mKeyOffsets[i - 1]] + 1);

	mDirty = true;
}

bool M6IndexPage::GetNext(uint32& ioPage, uint32& ioIndex, M6Tuple& outTuple) const
{
	bool result = false;
	++ioIndex;
	if (ioIndex < mData->mN)
	{
		result = true;
		GetKeyValue(ioIndex, outTuple.key, outTuple.value);
	}
	else if (mData->mLink != 0)
	{
		ioPage = mData->mLink;
		M6IndexPagePtr next(mIndexImpl.Load(ioPage));
		ioIndex = 0;
		next->GetKeyValue(ioIndex, outTuple.key, outTuple.value);
		result = true;
	}
	
	return result;
}

void M6IndexPage::BinarySearch(const string& inKey, int32& outIndex, bool& outMatch) const
{
	outMatch = false;
	
	int32 L = 0, R = mData->mN - 1;
	while (L <= R)
	{
		int32 i = (L + R) / 2;

		const uint8* ko = mData->mKeys + mKeyOffsets[i];
		const char* k = reinterpret_cast<const char*>(ko + 1);

		int d = mIndexImpl.CompareKeys(inKey.c_str(), inKey.length(), k, *ko);
		if (d == 0)
		{
			outIndex = i;
			outMatch = true;
			break;
		}
		else if (d < 0)
			R = i - 1;
		else
			L = i + 1;
	}

	if (not outMatch)
		outIndex = R;
}

void M6IndexPage::EraseEntry(uint32 inIndex)
{
	assert(inIndex < mData->mN);
	assert(mData->mN <= kM6MaxEntriesPerPage);
	
	if (mData->mN > 1)
	{
		void* src = mData->mKeys + mKeyOffsets[inIndex + 1];
		void* dst = mData->mKeys + mKeyOffsets[inIndex];
		uint32 n = mKeyOffsets[mData->mN] - mKeyOffsets[inIndex + 1];
		memmove(dst, src, n);
		
		src = mData->mData + kM6IndexPageDataCount - mData->mN;
		dst = mData->mData + kM6IndexPageDataCount - mData->mN + 1;
		n = (mData->mN - inIndex - 1) * sizeof(int64);
		memmove(dst, src, n);

		for (int i = inIndex + 1; i <= mData->mN; ++i)
			mKeyOffsets[i] = mKeyOffsets[i - 1] + mData->mKeys[mKeyOffsets[i - 1]] + 1;
	}
	
	--mData->mN;
	mDirty = true;
}

void M6IndexPage::ReplaceKey(uint32 inIndex, const string& inKey)
{
	assert(inIndex < mData->mN);
	assert(mData->mN <= kM6MaxEntriesPerPage);

	uint8* k = mData->mKeys + mKeyOffsets[inIndex];
	
	int32 delta = static_cast<int32>(inKey.length()) - *k;
	assert(delta < 0 or Free() >= static_cast<uint32>(delta));
	
	if (inIndex + 1 < mData->mN)
	{
		uint8* src = k + *k + 1;
		uint8* dst = src + delta;
		uint32 n = mKeyOffsets[mData->mN] - mKeyOffsets[inIndex + 1];
		memmove(dst, src, n);
	}
	
	*k = static_cast<uint8>(inKey.length());
	memcpy(k + 1, inKey.c_str(), inKey.length());

	for (int i = inIndex + 1; i <= mData->mN; ++i)
		mKeyOffsets[i] += delta;
	
	mDirty = true;
}

// --------------------------------------------------------------------

M6IndexLeafPage::M6IndexLeafPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
	: M6IndexPage(inIndexImpl, inData, inPageNr)
{
	mData->mType = eM6IndexSimpleLeafPage;
}

bool M6IndexLeafPage::Find(const string& inKey, int64& outValue)
{
	bool match;
	int32 ix;
	
	BinarySearch(inKey, ix, match);
	if (match)
		outValue = GetValue(ix);
//#if DEBUG
//	else
//		cout << "Key not found: " << inKey << endl;
//#endif
	
	return match;
}

/*
	Insert returns a bool indicating the depth increased.
	In that case the ioKey and ioValue are updated to the values
	to be inserted in the calling page (or a new root has to be made).
*/

bool M6IndexLeafPage::Insert(string& ioKey, int64& ioValue, M6IndexBranchPage* inParent)
{
	bool result = false;

	int32 ix;
	bool match;
	BinarySearch(ioKey, ix, match);
	
	if (match)
		SetValue(ix, ioValue);	// simply update the value (we're a unique index)
	else if (CanStore(ioKey))
		InsertKeyValue(ioKey, ioValue, ix + 1);
	else
	{
		ix += 1;	// we need to insert at ix + 1

		// create a new leaf page
		M6IndexPagePtr next(mIndexImpl.Allocate<M6IndexLeafPage>());
	
		uint32 split = mData->mN / 2;

		MoveEntries(*this, *next, split, 0, mData->mN - split);
		next->SetLink(mData->mLink);
		mData->mLink = next->GetPageNr();
		
		if (ix <= mData->mN)
			InsertKeyValue(ioKey, ioValue, ix);
		else
			next->InsertKeyValue(ioKey, ioValue, ix - mData->mN);
		
		ioKey = next->GetKey(0);
		ioValue = next->GetPageNr();
		
		result = true;
	}

//	assert(mData->mN >= kM6MinEntriesPerPage or inParent == nullptr);
	assert(mData->mN <= kM6MaxEntriesPerPage);

	return result;
}

bool M6IndexLeafPage::Erase(string& ioKey, int32 inIndex, M6IndexBranchPage* inParent, M6IndexBranchPage* inLinkPage, uint32 inLinkIndex)
{
	bool result = false, match = false;
	int32 ix;
	BinarySearch(ioKey, ix, match);
	
	if (match)		// match in a leaf page
	{
		result = true;
		
		// erase the key at ix
		EraseEntry(ix);
		
		if (inParent != nullptr)
		{
			assert(mData->mN > 0);
			if (ix == 0 and mData->mN > 0 and inLinkPage != nullptr)	// need to pass on the new key
			{
				assert(inLinkPage->GetKey(inLinkIndex) == ioKey);
				
				// replace the link key in the branch page passed in inLinkPage.
				// However, if it doesn't fit, we'll just leave it there. I think
				// that is not a serious problem, it means there will be a key in a
				// branch page that is less than the first key in the leaf page it
				// eventually leads to. This won't interfere with the rest of the
				// algorithms.
				
				string key = GetKey(0);
				int32 delta = static_cast<int32>(key.length() - ioKey.length());
				if (delta < 0 or delta < static_cast<int32>(inLinkPage->Free()))
					inLinkPage->ReplaceKey(inLinkIndex, GetKey(0));
			}
		
			if (TooSmall())
			{							// we're not the root page and we've lost too many entries
				if (inIndex + 1 < static_cast<int32>(inParent->GetN()))
				{
					// try to compensate using our right sibling
					M6IndexPagePtr right(mIndexImpl.Load(inParent->GetValue(inIndex + 1)));
					Underflow(*right, inIndex + 1, inParent);
				}
				
				if (TooSmall() and inIndex >= 0)
				{
					// if still too small, try with the left sibling
					uint32 leftNr;
					if (inIndex > 0)
						leftNr = inParent->GetValue(inIndex - 1);
					else
						leftNr = inParent->GetLink();

					M6IndexPagePtr left(mIndexImpl.Load(leftNr));
					left->Underflow(*this, inIndex, inParent);
				}
				
				result = true;
			}
		}
	}
	
	return result;
}

bool M6IndexLeafPage::Underflow(M6IndexPage& inRight, uint32 inIndex, M6IndexBranchPage* inParent)
{
	M6IndexLeafPage& right(dynamic_cast<M6IndexLeafPage&>(inRight));
	
	// Page left of right contains too few entries, see if we can fix this
	// first try a merge
	if (Free() + right.Free() >= kM6IndexPageKeySpace and
		mData->mN + right.mData->mN <= kM6MaxEntriesPerPage)
	{
		// join the pages
		MoveEntries(right, *this, 0, mData->mN, right.mData->mN);
		mData->mLink = right.mData->mLink;
	
		inParent->EraseEntry(inIndex);
		inRight.Deallocate();
	}
	else		// redistribute the data
	{
		// pKey is the key in inParent at inIndex (and, since this a leaf, the first key in right)
		string pKey = inParent->GetKey(inIndex);
		assert(pKey == right.GetKey(0));
		int32 pKeyLen = static_cast<int32>(pKey.length());
		int32 pFree = inParent->Free();
		
		if (Free() > right.Free() and mData->mN < kM6IndexPageDataCount)	// move items from right to left
		{
			assert(TooSmall());

			int32 delta = Free() - right.Free();
			int32 needed = delta / 2;
			
			uint8* rk = right.mData->mKeys;
			uint32 n = 0, ln = 0;
			while (n < right.mData->mN and n + mData->mN < kM6IndexPageDataCount and needed > *rk)
			{
				++n;
				if ((*rk - pKeyLen + pFree) > 0)	// if the new first key of right fits in the parent
					ln = n;							// we have a candidate
				needed -= *rk + sizeof(int64);
				rk += *rk + 1;
			}
			
			// move the data
			MoveEntries(right, *this, 0, mData->mN, ln);
			inParent->ReplaceKey(inIndex, right.GetKey(0));
		}
		else if (right.Free() > Free() and right.mData->mN < kM6IndexPageDataCount)
		{
			assert(right.TooSmall());

			int32 delta = right.Free() - Free();
			int32 needed = delta / 2;
			
			uint8* rk = mData->mKeys + mKeyOffsets[mData->mN - 1];
			uint32 n = 0, ln = 0;
			while (n < mData->mN and n + right.mData->mN < kM6IndexPageDataCount and needed > *rk)
			{
				++n;
				if ((*rk - pKeyLen + pFree) > 0)	// if the new first key of right fits in the parent
					ln = n;							// we have a candidate
				needed -= *rk + sizeof(int64);
				rk = mData->mKeys + mKeyOffsets[mData->mN - 1 - n];
			}
			
			// move the data
			MoveEntries(*this, right, mData->mN - ln, 0, ln);
			inParent->ReplaceKey(inIndex, right.GetKey(0));
		}
	}
	
	return not (TooSmall() or right.TooSmall());
}

// --------------------------------------------------------------------

M6IndexImpl::M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, MOpenMode inMode)
	: mFile(inPath, inMode)
	, mIndex(inIndex)
	, mDirty(false)
	, mAutoCommit(true)
{
	InitCache();	
	
	if (inMode == eReadWrite and mFile.Size() == 0)
	{
		M6IxFileHeaderPage page = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
		mFile.PWrite(&page, kM6IndexPageSize, 0);

		// and create a root page to keep code simple
		M6IndexPagePtr root(Allocate<M6IndexLeafPage>());

		mHeader = page.mHeader;
		mHeader.mRoot = root->GetPageNr();
		mHeader.mDepth = 1;
		mDirty = true;
		
		mFile.PWrite(mHeader, 0);
	}
	else
		mFile.PRead(mHeader, 0);
	
	assert(mHeader.mSignature == kM6IndexFileSignature);
	assert(mHeader.mHeaderSize == sizeof(M6IxFileHeader));
}

M6IndexImpl::M6IndexImpl(M6BasicIndex& inIndex, const string& inPath,
		M6SortedInputIterator& inData)
	: mFile(inPath, eReadWrite)
	, mIndex(inIndex)
	, mDirty(false)
	, mAutoCommit(true)
{
	InitCache();	

	mFile.Truncate(0);
	
	M6IxFileHeaderPage data = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
	mFile.PWrite(&data, kM6IndexPageSize, 0);

	mHeader = data.mHeader;
	mFile.PWrite(mHeader, 0);
	
	string last;
	
	try
	{
		// inData is sorted, so we start by writing out leaf pages:
		M6IndexPagePtr page(Allocate<M6IndexLeafPage>());
		
		deque<M6Tuple> up;		// keep track of the nodes we have to create for the next levels
		M6Tuple tuple;
		
		tuple.value = page->GetPageNr();
		up.push_back(tuple);
		
		while (inData(tuple))
		{
			// sanity check
			if (CompareKeys(last, tuple.key) >= 0)
				THROW(("Trying to build index from unsorted data"));
			
			if (not page->CanStore(tuple.key))
			{
				M6IndexPagePtr next(Allocate<M6IndexLeafPage>());
				page->SetLink(next->GetPageNr());
				page.reset(next.release());
	
				up.push_back(tuple);
				up.back().value = page->GetPageNr();
			}
			
			page->InsertKeyValue(tuple.key, tuple.value, page->GetN());
			++mHeader.mSize;
		}
		
		// all data is written in the leafs, now construct branch pages
		CreateUpLevels(up);
	}
	catch (...)
	{
		Rollback();
		throw;
	}

	Commit();
}

M6IndexImpl::~M6IndexImpl()
{
	if (mDirty)
		mFile.PWrite(mHeader, 0);
	delete[] mCache;
}

void M6IndexImpl::InitCache()
{
	mCacheCount = kM6CacheCount;
	mCache = new M6CachedPage[mCacheCount];
	
	for (uint32 ix = 0; ix < mCacheCount; ++ix)
	{
		mCache[ix].mPageNr = 0;
		mCache[ix].mNext = mCache + ix + 1;
		mCache[ix].mPrev = mCache + ix - 1;
	}
	
	mCache[0].mPrev = mCache[mCacheCount - 1].mNext = nullptr;
	mLRUHead = mCache;
	mLRUTail = mCache + mCacheCount - 1;
}

M6IndexImpl::M6CachedPagePtr M6IndexImpl::GetCachePage(uint32 inPageNr)
{
	M6CachedPagePtr result = mLRUTail;
	
	// now search backwards for a cached page that can be recycled
	uint32 n = 0;
	while (result != nullptr and result->mPage and 
		(result->mPage->GetRefCount() > 1 or result->mPage->IsDirty()))
	{
		result = result->mPrev;
		++n;
	}
	
	// we could end up with a full cache, if so, double the cache
	
	if (result == nullptr)
	{
		mCacheCount *= 2;
		M6CachedPagePtr tmp = new M6CachedPage[mCacheCount];
		
		for (uint32 ix = 0; ix < mCacheCount; ++ix)
		{
			tmp[ix].mPageNr = 0;
			tmp[ix].mNext = tmp + ix + 1;
			tmp[ix].mPrev = tmp + ix - 1;
		}

		tmp[0].mPrev = tmp[mCacheCount - 1].mNext = nullptr;

		for (M6CachedPagePtr a = mLRUHead, b = tmp; a != nullptr; a = a->mNext, b = b->mNext)
		{
			b->mPageNr = a->mPageNr;
			b->mPage = a->mPage;
		}
		
		delete[] mCache;
		mCache = tmp;
		
		mLRUHead = mCache;
		mLRUTail = mCache + mCacheCount - 1;
		
		result = mLRUTail;
	}

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
	
	result->mPageNr = inPageNr;
	return result;
}

template<class P>
M6IndexPagePtr M6IndexImpl::Allocate()
{
	int64 fileSize = mFile.Size();
	int64 pageNr = (fileSize - 1) / kM6IndexPageSize + 1;
	int64 offset = pageNr * kM6IndexPageSize;
	mFile.Truncate(offset + kM6IndexPageSize);
	
	M6IndexPageData* data = new M6IndexPageData;
	memset(data, 0, kM6IndexPageSize);
	
	M6CachedPagePtr cp = GetCachePage(pageNr);
	cp->mPage.reset(new P(*this, data, pageNr));
	return cp->mPage;
}

M6IndexPagePtr M6IndexImpl::Load(uint32 inPageNr)
{
	if (inPageNr == 0)
		THROW(("Invalid page number"));

	M6CachedPagePtr cp = mLRUHead;
	while (cp != nullptr and cp->mPageNr != inPageNr)
		cp = cp->mNext; 

	if (cp == nullptr)
	{
		cp = GetCachePage(inPageNr);
		
		M6IndexPageData* data = new M6IndexPageData;
		mFile.PRead(*data, inPageNr * kM6IndexPageSize);

		if (data->mType == eM6IndexBranchPage)
			cp->mPage.reset(new M6IndexBranchPage(*this, data, inPageNr));
		else
			cp->mPage.reset(new M6IndexLeafPage(*this, data, inPageNr));
	}
	
	return cp->mPage;
}

void M6IndexImpl::Commit()
{
	for (uint32 ix = 0; ix < mCacheCount; ++ix)
	{
		if (mCache[ix].mPage and mCache[ix].mPage->IsDirty())
			mCache[ix].mPage->Flush(mFile);
	}
}

void M6IndexImpl::Rollback()
{
	for (uint32 ix = 0; ix < mCacheCount; ++ix)
	{
		if (mCache[ix].mPage and mCache[ix].mPage->IsDirty())
		{
			mCache[ix].mPage->SetDirty(false);
			mCache[ix].mPage->Release();
			mCache[ix].mPageNr = 0;
		}
	}
}

void M6IndexImpl::SetAutoCommit(bool inAutoCommit)
{
	mAutoCommit = inAutoCommit;
}

void M6IndexImpl::Insert(const string& inKey, int64 inValue)
{
	try
	{
		M6IndexPagePtr root(Load(mHeader.mRoot));
		
		string key(inKey);
		int64 value(inValue);
	
		if (root->Insert(key, value, nullptr))
		{
			// increase depth
			++mHeader.mDepth;
	
			M6IndexPagePtr newRoot(Allocate<M6IndexBranchPage>());
			newRoot->SetLink(mHeader.mRoot);
			newRoot->InsertKeyValue(key, value, 0);
			mHeader.mRoot = newRoot->GetPageNr();
		}
	
		if (mAutoCommit)
			Commit();
	
		++mHeader.mSize;
		mDirty = true;
	}
	catch (...)
	{
		Rollback();
		throw;
	}	
}

bool M6IndexImpl::Erase(const string& inKey)
{
	bool result = false;
	
	try
	{
		M6IndexPagePtr root(Load(mHeader.mRoot));
		
		string key(inKey);
	
		if (root->Erase(key, 0, nullptr, nullptr, 0))
		{
			result = true;
			
			if (root->GetN() == 0 and mHeader.mDepth > 1)
			{
				mHeader.mRoot = root->GetLink();
				root->Deallocate();
				--mHeader.mDepth;
			}
		}
		
		if (mAutoCommit)
			Commit();
	
		--mHeader.mSize;
		mDirty = true;
	}
	catch (...)
	{
		Rollback();
		throw;
	}
	
	return result;
}

bool M6IndexImpl::Find(const string& inKey, int64& outValue)
{
	M6IndexPagePtr root(Load(mHeader.mRoot));
	return root->Find(inKey, outValue);
}

void M6IndexImpl::Vacuum()
{
	try
	{
		// start by locating the first leaf node.
		// Then compact the nodes and shift them to the start
		// of the file. Truncate the file and reconstruct the
		// branch nodes.
	
		uint32 pageNr = mHeader.mRoot;
		for (;;)
		{
			M6IndexPagePtr page(Load(pageNr));
			if (page->IsLeaf())
				break;
			pageNr = page->GetLink();
		}
	
		// keep an indirect array of reordered pages
		size_t pageCount = (mFile.Size() / kM6IndexPageSize) + 1;
		vector<uint32> ix1(pageCount);
		iota(ix1.begin(), ix1.end(), 0);
		vector<uint32> ix2(ix1);

		deque<M6Tuple> up;
		uint32 n = 1;

		for (;;)
		{
			pageNr = ix1[pageNr];
			M6IndexPagePtr page(Load(pageNr));
			if (pageNr != n)
			{
				swap(ix1[ix2[pageNr]], ix1[ix2[n]]);
				swap(ix2[pageNr], ix2[n]);
				page->MoveTo(n);
			}

			up.push_back(M6Tuple(page->GetKey(0), page->GetPageNr()));
			uint32 link = page->GetLink();
		
			while (link != 0)
			{
				M6IndexPagePtr next(Load(ix1[link]));
				if (next->GetN() == 0)
				{
					link = next->GetLink();
					continue;
				}

				string key = next->GetKey(0);
				assert(key.compare(page->GetKey(page->GetN() - 1)) > 0);
				if (not page->CanStore(key))
					break;
			
				page->InsertKeyValue(key, next->GetValue(0), page->GetN());
				next->EraseEntry(0);
			}
		
			if (link == 0)
			{
				page->SetLink(0);
				break;
			}

			pageNr = link;

			++n;
			page->SetLink(n);
		}
	
		// OK, so we have all the pages on disk, in order.
		// truncate the file (erasing the remaining pages)
		// and rebuild the branches.
		mFile.Truncate((n + 1) * kM6IndexPageSize);
	
		CreateUpLevels(up);

		Commit();
	}
	catch (...)
	{
		Rollback();
		throw;
	}
}

void M6IndexImpl::CreateUpLevels(deque<M6Tuple>& up)
{
	mHeader.mDepth = 1;
	while (up.size() > 1)
	{
		++mHeader.mDepth;

		deque<M6Tuple> nextUp;
		
		// we have at least two tuples, so take the first and use it as
		// link, and the second as first entry for the first page
		M6Tuple tuple = up.front();
		up.pop_front();
		
		M6IndexPagePtr page(Allocate<M6IndexBranchPage>());
		page->SetLink(tuple.value);
		
		// store this new page for the next round
		nextUp.push_back(M6Tuple(tuple.key, page->GetPageNr()));
		
		while (not up.empty())
		{
			tuple = up.front();
			
			// make sure we never end up with an empty page
			if (page->CanStore(tuple.key))
			{
				if (up.size() == 1)
				{
					page->InsertKeyValue(tuple.key, tuple.value, page->GetN());
					up.pop_front();
					break;
				}
				
				// special case, if up.size() == 2 and we can store both
				// keys, store them and break the loop
				if (up.size() == 2 and
					page->GetN() + 1 < kM6MaxEntriesPerPage and
					page->Free() >= (up[0].key.length() + up[1].key.length() + 2 + 2 * sizeof(int64)))
				{
					page->InsertKeyValue(up[0].key, up[0].value, page->GetN());
					page->InsertKeyValue(up[1].key, up[1].value, page->GetN());
					break;
				}
				
				// otherwise, only store the key if there's enough
				// in up to avoid an empty page
				if (up.size() > 2)
				{
					page->InsertKeyValue(tuple.key, tuple.value, page->GetN());
					up.pop_front();
					continue;
				}
			}

			// cannot store the tuple, create new page
			page = Allocate<M6IndexBranchPage>();
			page->SetLink(tuple.value);

			nextUp.push_back(M6Tuple(tuple.key, page->GetPageNr()));
			up.pop_front();
		}
		
		up = nextUp;
	}
	
	assert(up.size() == 1);
	mHeader.mRoot = up.front().value;
	mDirty = true;
}

M6BasicIndex::iterator M6IndexImpl::Begin()
{
	uint32 pageNr = mHeader.mRoot;
	for (;;)
	{
		M6IndexPagePtr page(Load(pageNr));
		if (page->IsLeaf())
			break;
		pageNr = page->GetLink();
	}
	
	return M6BasicIndex::iterator(this, pageNr, 0);
}

M6BasicIndex::iterator M6IndexImpl::End()
{
	return M6BasicIndex::iterator(nullptr, 0, 0);
}

// --------------------------------------------------------------------

M6BasicIndex::iterator::iterator()
	: mIndex(nullptr)
	, mPage(0)
	, mKeyNr(0)
{
}

M6BasicIndex::iterator::iterator(const iterator& iter)
	: mIndex(iter.mIndex)
	, mPage(iter.mPage)
	, mKeyNr(iter.mKeyNr)
	, mCurrent(iter.mCurrent)
{
}

M6BasicIndex::iterator::iterator(M6IndexImpl* inImpl, uint32 inPageNr, uint32 inKeyNr)
	: mIndex(inImpl)
	, mPage(inPageNr)
	, mKeyNr(inKeyNr)
{
	if (mIndex != nullptr)
	{
		M6IndexPagePtr page(mIndex->Load(mPage));
		page->GetKeyValue(mKeyNr, mCurrent.key, mCurrent.value);
	}
}

M6BasicIndex::iterator& M6BasicIndex::iterator::operator=(const iterator& iter)
{
	if (this != &iter)
	{
		mIndex = iter.mIndex;
		mPage = iter.mPage;
		mKeyNr = iter.mKeyNr;
		mCurrent = iter.mCurrent;
	}
	
	return *this;
}

M6BasicIndex::iterator& M6BasicIndex::iterator::operator++()
{
	M6IndexPagePtr page(mIndex->Load(mPage));
	if (not page->GetNext(mPage, mKeyNr, mCurrent))
	{
		mIndex = nullptr;
		mPage = 0;
		mKeyNr = 0;
		mCurrent = M6Tuple();
	}

	return *this;
}

// --------------------------------------------------------------------

M6BasicIndex::M6BasicIndex(const string& inPath, MOpenMode inMode)
	: mImpl(new M6IndexImpl(*this, inPath, inMode))
{
}

M6BasicIndex::M6BasicIndex(const string& inPath, M6SortedInputIterator& inData)
	: mImpl(new M6IndexImpl(*this, inPath, inData))
{
}

M6BasicIndex::~M6BasicIndex()
{
	delete mImpl;
}

void M6BasicIndex::Vacuum()
{
	mImpl->Vacuum();
}

M6BasicIndex::iterator M6BasicIndex::begin() const
{
	return mImpl->Begin();
}

M6BasicIndex::iterator M6BasicIndex::end() const
{
	return mImpl->End();
}

void M6BasicIndex::insert(const string& key, int64 value)
{
	if (key.length() >= kM6MaxKeyLength)
		THROW(("Invalid key length"));

	mImpl->Insert(key, value);
}

void M6BasicIndex::erase(const string& key)
{
	mImpl->Erase(key);
}

bool M6BasicIndex::find(const string& inKey, int64& outValue)
{
	return mImpl->Find(inKey, outValue);
}

uint32 M6BasicIndex::size() const
{
	return mImpl->Size();
}

uint32 M6BasicIndex::depth() const
{
	return mImpl->Depth();
}

void M6BasicIndex::Commit()
{
	mImpl->Commit();
}

void M6BasicIndex::Rollback()
{
	mImpl->Rollback();
}

void M6BasicIndex::SetAutoCommit(bool inAutoCommit)
{
	mImpl->SetAutoCommit(inAutoCommit);
}

// DEBUG code

#if DEBUG

class M6ValidationException : public std::exception
{
  public:
					M6ValidationException(M6IndexPage* inPage, const char* inReason)
						: mPageNr(inPage->GetPageNr())
					{
#if defined(_MSC_VER)
						sprintf_s(mReason, sizeof(mReason), "%s", inReason);
#else
						snprintf(mReason, sizeof(mReason), "%s", inReason);
#endif
					}
			
	const char*		what() throw() { return mReason; }
		
	uint32			mPageNr;
	char			mReason[512];
};

#define M6VALID_ASSERT(cond)	do { if (not (cond)) throw M6ValidationException(this, #cond ); } while (false)

void M6IndexPage::Validate(const string& inKey, M6IndexBranchPage* inParent)
{
	if (IsLeaf())
	{
//		M6VALID_ASSERT(mData->mN >= kM6MinEntriesPerPage or inParent == nullptr);
		//M6VALID_ASSERT(inParent == nullptr or not TooSmall());
		M6VALID_ASSERT(inKey.empty() or GetKey(0) == inKey);
		
		for (uint32 i = 0; i < mData->mN; ++i)
		{
			if (i > 0)
			{
//				M6VALID_ASSERT(GetValue(i) > GetValue(i - 1));
				M6VALID_ASSERT(mIndexImpl.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
			}
		}
		
		if (mData->mLink != 0)
		{
			M6IndexPagePtr next(mIndexImpl.Load(mData->mLink));
			M6VALID_ASSERT(mIndexImpl.CompareKeys(GetKey(mData->mN - 1), next->GetKey(0)) < 0);
		}
	}
	else
	{
//		M6VALID_ASSERT(mData->mN >= kM6MinEntriesPerPage or inParent == nullptr);
		//M6VALID_ASSERT(inParent == nullptr or not TooSmall());
//		M6VALID_ASSERT(mData->mN <= kM6MaxEntriesPerPage);

		for (uint32 i = 0; i < mData->mN; ++i)
		{
			M6IndexPagePtr link(mIndexImpl.Load(mData->mLink));
			link->Validate(inKey, static_cast<M6IndexBranchPage*>(this));
			
			for (uint32 i = 0; i < mData->mN; ++i)
			{
				M6IndexPagePtr page(mIndexImpl.Load(GetValue(i)));
				page->Validate(GetKey(i), static_cast<M6IndexBranchPage*>(this));
				if (i > 0)
					M6VALID_ASSERT(mIndexImpl.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
			}
		}
	}
}

void M6IndexPage::Dump(int inLevel, M6IndexBranchPage* inParent)
{
	string prefix(inLevel * 2, ' ');

	if (IsLeaf())
	{
		cout << prefix << "leaf page at " << mPageNr << "; N = " << mData->mN << ": [";
		for (int i = 0; i < mData->mN; ++i)
			cout << GetKey(i) << '(' << GetValue(i) << ')'
				 << (i + 1 < mData->mN ? ", " : "");
		cout << "]" << endl;

		if (mData->mLink)
		{
			M6IndexPagePtr next(mIndexImpl.Load(mData->mLink));
			cout << prefix << "  " << "link: " << next->GetKey(0) << endl;
		}
	}
	else
	{
		cout << prefix << (inParent ? "branch" : "root") << " page at " << mPageNr << "; N = " << mData->mN << ": {";
		for (int i = 0; i < mData->mN; ++i)
			cout << GetKey(i) << (i + 1 < mData->mN ? ", " : "");
		cout << "}" << endl;

		M6IndexPagePtr link(mIndexImpl.Load(mData->mLink));
		link->Dump(inLevel + 1, static_cast<M6IndexBranchPage*>(this));
		
		for (int i = 0; i < mData->mN; ++i)
		{
			cout << prefix << inLevel << '.' << i << ") " << GetKey(i) << endl;
			
			M6IndexPagePtr sub(mIndexImpl.Load(GetValue(i)));
			sub->Dump(inLevel + 1, static_cast<M6IndexBranchPage*>(this));
		}
	}
}

void M6IndexImpl::Validate()
{
	try
	{
		M6IndexPagePtr root(Load(mHeader.mRoot));
		root->Validate("", nullptr);
	}
	catch (M6ValidationException& e)
	{
		cout << endl
			 << "=================================================================" << endl
			 << "validation failed:" << endl
			 << "page: " << e.mPageNr << endl
			 << e.what() << endl;
		Dump();
		abort();
	}
}

void M6IndexImpl::Dump()
{
	cout << endl
		 << "Dumping tree" << endl
		 << endl;

	M6IndexPagePtr root(Load(mHeader.mRoot));
	root->Dump(0, nullptr);
}

void M6BasicIndex::dump() const
{
	mImpl->Dump();
}

void M6BasicIndex::validate() const
{
	mImpl->Validate();
}

#endif

// --------------------------------------------------------------------

M6MultiBasicIndex::M6MultiBasicIndex(const string& inPath, MOpenMode inMode)
	: M6BasicIndex(inPath, inMode)
{
}

void M6MultiBasicIndex::Insert(const string& inKey, const vector<uint32>& inDocuments,
	uint32 inMaxDocValue)
{
}

bool M6MultiBasicIndex::Find(const string& inKey, iterator& outIterator)
{
	return false;
}

// --------------------------------------------------------------------

M6MultiIDLBasicIndex::M6MultiIDLBasicIndex(const string& inPath, MOpenMode inMode)
	: M6BasicIndex(inPath, inMode)
{
}

void M6MultiIDLBasicIndex::Insert(const string& inKey, int64 inIDLOffset,
	const vector<uint32>& inDocuments, uint32 inMaxDocValue)
{
}

bool M6MultiIDLBasicIndex::Find(const string& inKey, iterator& outIterator, int64& outIDLOffset)
{
	return false;
}

// --------------------------------------------------------------------

M6WeightedBasicIndex::M6WeightedBasicIndex(const string& inPath, MOpenMode inMode)
	: M6BasicIndex(inPath, inMode)
{
}

void M6WeightedBasicIndex::Insert(const string& inKey,
	const vector<pair<uint32,uint8>>& inDocuments, uint32 inMaxDocValue)
{
}

bool M6WeightedBasicIndex::Find(const string& inKey, weighted_iterator& outIterator)
{
	return false;
}
