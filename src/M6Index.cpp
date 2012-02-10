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
	eM6IndexMultiIDLLeafPage	= 'i',
	eM6IndexBitVectorPage		= 'v'
};

struct M6IndexPageHeader
{
	M6IndexPageType	mType;
	uint8			mFlags;
	uint16			mN;
	uint32			mLink;
};

const uint32
	kM6MaxKeyLength			= 255,
	kM6IndexPageSize		= 8192,
//	kM6IndexPageSize		= 256,
	kM6IndexPageHeaderSize	= sizeof(M6IndexPageHeader),
	kM6KeySpace				= kM6IndexPageSize - kM6IndexPageHeaderSize,
	kM6MinKeySpace			= kM6KeySpace / 2,
//	kM6MaxEntriesPerPage	= 4;
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

struct M6MultiData
{
	uint32	mCount;
	int64	mBitVector;
};

ostream& operator<<(ostream& os, const M6MultiData& d)
{
	os << '<' << d.mCount << ',' << d.mBitVector << '>';
	return os;
}

template<>
struct M6IndexPageDataTraits<eM6IndexMultiLeafPage>
{
	typedef M6MultiData M6DataElement;
};

struct M6MultiIDLData
{
	uint32	mCount;
	int64	mBitVector;
	int64	mIDLOffset;
};

ostream& operator<<(ostream& os, const M6MultiIDLData& d)
{
	os << '<' << d.mCount << ',' << d.mBitVector << ',' << d.mIDLOffset << '>';
	return os;
}

template<>
struct M6IndexPageDataTraits<eM6IndexMultiIDLLeafPage>
{
	typedef M6MultiIDLData M6DataElement;
};

template<M6IndexPageType T>
struct M6IndexPageDataT : public M6IndexPageHeader
{
	typedef typename M6IndexPageDataTraits<T>::M6DataElement M6DataType;
	
	enum {
		kM6DataCount	= kM6KeySpace / sizeof(M6DataType),
		kM6EntryCount	= (kM6DataCount < kM6MaxEntriesPerPage ? kM6DataCount : kM6MaxEntriesPerPage)
	};
	
	static const M6IndexPageType kIndexPageType = T;
	
	union
	{
		uint8		mKeys[kM6KeySpace];
		M6DataType	mData[kM6DataCount];
	};
};

template<>
struct M6IndexPageDataT<eM6IndexBitVectorPage> : public M6IndexPageHeader
{
	static const M6IndexPageType kIndexPageType = eM6IndexBitVectorPage;
	uint8	mBits[kM6KeySpace];
};

typedef M6IndexPageDataT<eM6IndexBranchPage>		M6IndexBranchPageData;
typedef M6IndexPageDataT<eM6IndexSimpleLeafPage>	M6IndexSimpleLeafPageData;
typedef M6IndexPageDataT<eM6IndexMultiLeafPage>		M6IndexMultiLeafPageData;
typedef M6IndexPageDataT<eM6IndexMultiIDLLeafPage>	M6IndexMultiIDLLeafPageData;
typedef M6IndexPageDataT<eM6IndexBitVectorPage>		M6IndexBitVectorPageData;

union M6IndexPageData
{
	M6IndexBranchPageData		branch;
	M6IndexSimpleLeafPageData	leaf;
	M6IndexMultiLeafPageData	multi_leaf;
	M6IndexMultiIDLLeafPageData	idl_leaf;
	M6IndexBitVectorPageData	bit_vector;
};

BOOST_STATIC_ASSERT(sizeof(M6IndexPageData) == kM6IndexPageSize);

template<class M6DataType>
struct M6IndexPageDataTypeFactory
{
};

template<>
struct M6IndexPageDataTypeFactory<M6IndexPageDataTraits<eM6IndexSimpleLeafPage>::M6DataElement>
{
	typedef M6IndexSimpleLeafPageData M6LeafPageDataType;
};

template<>
struct M6IndexPageDataTypeFactory<M6IndexPageDataTraits<eM6IndexMultiLeafPage>::M6DataElement>
{
	typedef M6IndexMultiLeafPageData M6LeafPageDataType;
};

template<>
struct M6IndexPageDataTypeFactory<M6IndexPageDataTraits<eM6IndexMultiIDLLeafPage>::M6DataElement>
{
	typedef M6IndexMultiIDLLeafPageData M6LeafPageDataType;
};

// --------------------------------------------------------------------

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
class M6IndexLeafPage;

template<class M6DataType>
class M6IndexBranchPage;

// --------------------------------------------------------------------

struct M6IndexImpl
{
					M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, MOpenMode inMode);
//					M6IndexImpl(M6BasicIndex& inIndex, const string& inPath,
//						M6SortedInputIterator& inData);
	virtual 		~M6IndexImpl();

	//iterator		Begin();
	//iterator		End();

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
	virtual void	Validate() = 0;
	virtual void	Dump() = 0;
#endif

	void			SetAutoCommit(bool inAutoCommit);

	virtual void	Commit() = 0;
	virtual void	Rollback() = 0;
	virtual void	Vacuum() = 0;
	
  protected:

	M6File			mFile;
	M6BasicIndex&	mIndex;
	M6IxFileHeader	mHeader;
	bool			mDirty;
	bool			mAutoCommit;
};

template<class M6DataType>
class M6IndexImplT : public M6IndexImpl
{
  public:
	typedef typename M6BasicIndex::iterator	iterator;
	
	typedef M6IndexPage<M6DataType>			M6IndexPageType;
	typedef M6IndexLeafPage<M6DataType>		M6LeafPageType;
	typedef M6IndexBranchPage<M6DataType>	M6BranchPageType;

	typedef M6IndexPageType*				M6IndexPagePtr;
	typedef M6LeafPageType*					M6LeafPagePtr;
	typedef M6BranchPageType*				M6BranchPagePtr;

					M6IndexImplT(M6BasicIndex& inIndex, const string& inPath, MOpenMode inMode);
//					M6IndexImplT(M6BasicIndex& inIndex, const string& inPath,
//						M6SortedInputIterator& inData);
	virtual 		~M6IndexImplT();

	void			Insert(const string& inKey, const M6DataType& inValue);
	bool			Erase(const string& inKey);
	bool			Find(const string& inKey, M6DataType& outValue);

	virtual void	Commit();
	virtual void	Rollback();
	virtual void	Vacuum();

	// page cache
	M6LeafPagePtr	AllocateLeaf();
	M6BranchPagePtr	AllocateBranch();
	M6IndexPagePtr	Load(uint32 inPageNr);
	void			Release(M6IndexPageType*& inPage);

	void			CreateUpLevels(deque<pair<string,M6DataType>>& up);

#if DEBUG
	virtual void	Validate();
	virtual void	Dump();
#endif

  private:

	struct M6CachedPage;
	typedef M6CachedPage*	M6CachedPagePtr;

	struct M6CachedPage
	{
#pragma message("TODO: Uitbreiden met dirty, refcount")
		uint32			mPageNr;
		M6IndexPageType*mPage;
		uint32			mRefCount;
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
	typedef M6IndexImplT<M6DataType>				M6IndexImplType;
	typedef M6IndexPage<M6DataType>					M6IndexPageType;
	typedef M6IndexBranchPage<M6DataType>			M6IndexBranchPageType;
	typedef M6IndexPageType*						M6IndexPagePtr;
	typedef M6IndexBranchPageType*					M6BranchPagePtr;
	  
	virtual 		~M6IndexPage();

	void			Deallocate();
	void			Flush(M6File& inFile);

	uint32			GetPageNr() const				{ return mPageNr; }
	void			MoveTo(uint32 inPageNr);

	bool			IsLeaf() const					{ return mData->mType != eM6IndexBranchPage; }
	
	bool			IsDirty() const					{ return mDirty; }
	void			SetDirty(bool inDirty)			{ mDirty = inDirty; }
	
	uint32			GetN() const					{ return mData->mN; }
	virtual uint32	Free() const = 0;
	virtual bool	CanStore(const string& inKey) const = 0;

	void			SetLink(uint32 inLink)			{ mData->mLink = inLink; mDirty = true; }
	uint32			GetLink() const					{ return mData->mLink; }
	
	bool			TooSmall() const				{ return Free() > kM6MinKeySpace; }

	virtual bool	Find(const string& inKey, M6DataType& outValue) = 0;
	virtual bool	Insert(string& ioKey, const M6DataType& inValue, uint32& outLink, M6IndexBranchPageType* inParent) = 0;
	virtual bool	Erase(string& ioKey, int32 inIndex, M6IndexBranchPageType* inParent, M6IndexBranchPageType* inLinkPage, uint32 inLinkIndex) = 0;

#if DEBUG
	virtual void	Validate(const string& inKey, M6IndexBranchPageType* inParent) = 0;
	virtual void	Dump(int inLevel, M6IndexBranchPageType* inParent) = 0;
#endif

	virtual bool	Underflow(M6IndexPageType& inRight, uint32 inIndex, M6IndexBranchPageType* inParent) = 0;

  protected:
					M6IndexPage(M6IndexImplType& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr);

	M6IndexImplType&
					mIndexImpl;
	M6IndexPageHeader*
					mData;
	uint32			mPageNr;
	bool			mLocked;
	bool			mDirty;

  private:

					M6IndexPage(const M6IndexPage&);
	M6IndexPage&	operator=(const M6IndexPage&);
};

// --------------------------------------------------------------------

template<class M6DataType>
M6IndexPage<M6DataType>::M6IndexPage(M6IndexImplType& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
	: mIndexImpl(inIndexImpl)
	, mPageNr(inPageNr)
	, mData(&inData->branch)
	, mLocked(false)
	, mDirty(false)
{
}

template<class M6DataType>
M6IndexPage<M6DataType>::~M6IndexPage()
{
	assert(not mDirty);
	delete mData;
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

template<class M6DataType>
void M6IndexPage<M6DataType>::MoveTo(uint32 inPageNr)
{
	if (inPageNr != mPageNr)
	{
		M6IndexPagePtr page(mIndexImpl.Load(inPageNr));

		page->mPageNr = mPageNr;
		if (page->IsLeaf())	// only save page if it is a leaf
			page->mDirty = true;
		
		mPageNr = inPageNr;
		mDirty = true;
		
		Release(page);
	}
}

// --------------------------------------------------------------------

template<class M6DataType, class M6DataPage>
class M6IndexPageT : public M6IndexPage<M6DataType>
{
  public:
	typedef M6DataPage							M6DataPageType;
	typedef typename M6DataPage::M6DataType		M6ValueType;
	
	enum {
		kM6DataCount = M6DataPageType::kM6DataCount,
		kM6EntryCount = M6DataPageType::kM6EntryCount
	};

					M6IndexPageT(M6IndexImplType& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr);

	virtual uint32	Free() const;
	virtual bool	CanStore(const string& inKey) const;

	void			BinarySearch(const string& inKey, int32& outIndex, bool& outMatch) const;

	string			GetKey(uint32 inIndex) const;
	M6ValueType		GetValue(uint32 inIndex) const;
	void			SetValue(uint32 inIndex, const M6ValueType& inValue);
	void			InsertKeyValue(const string& inKey, const M6ValueType& inValue, uint32 inIndex);
	void			GetKeyValue(uint32 inIndex, string& outKey, M6ValueType& outValue) const;
	void			EraseEntry(uint32 inIndex);
	void			ReplaceKey(uint32 inIndex, const string& inKey);

	static void		MoveEntries(M6IndexPageT& inFrom, M6IndexPageT& inTo,
						uint32 inFromOffset, uint32 inToOffset, uint32 inCount);

	M6DataPage&		mPageData;
	uint16			mKeyOffsets[kM6EntryCount + 1];
};

template<class M6DataType, class M6DataPage>
M6IndexPageT<M6DataType,M6DataPage>::M6IndexPageT(M6IndexImplType& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
	: M6IndexPage<M6DataType>(inIndexImpl, inData, inPageNr)
	, mPageData(*reinterpret_cast<M6DataPage*>(inData))
{
	uint8* key = mPageData.mKeys;
	for (uint32 i = 0; i <= mPageData.mN; ++i)
	{
		assert(i <= kM6EntryCount);
		mKeyOffsets[i] = static_cast<uint16>(key - mPageData.mKeys);
		key += *key + 1;
	}
}

template<class M6DataType, class M6DataPage>
uint32 M6IndexPageT<M6DataType,M6DataPage>::Free() const
{
	return kM6KeySpace - mKeyOffsets[mPageData.mN] - mPageData.mN * sizeof(M6ValueType);
}

template<class M6DataType, class M6DataPage>
bool M6IndexPageT<M6DataType,M6DataPage>::CanStore(const string& inKey) const
{
	return mPageData.mN < kM6EntryCount and Free() >= inKey.length() + 1 + sizeof(M6ValueType);
}

template<class M6DataType, class M6DataPage>
void M6IndexPageT<M6DataType,M6DataPage>::BinarySearch(const string& inKey, int32& outIndex, bool& outMatch) const
{
	outMatch = false;
	
	int32 L = 0, R = mPageData.mN - 1;
	while (L <= R)
	{
		int32 i = (L + R) / 2;

		const uint8* ko = mPageData.mKeys + mKeyOffsets[i];
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

template<class M6DataType, class M6DataPage>
inline string M6IndexPageT<M6DataType,M6DataPage>::GetKey(uint32 inIndex) const
{
	assert(inIndex < mPageData.mN);
	const uint8* key = mPageData.mKeys + mKeyOffsets[inIndex];
	return string(reinterpret_cast<const char*>(key) + 1, *key);
}

template<class M6DataType, class M6DataPage>
inline typename M6IndexPageT<M6DataType,M6DataPage>::M6ValueType M6IndexPageT<M6DataType,M6DataPage>::GetValue(uint32 inIndex) const
{
	assert(inIndex < mPageData.mN);
	return mPageData.mData[kM6DataCount - inIndex - 1];
}

template<class M6DataType, class M6DataPage>
inline void M6IndexPageT<M6DataType,M6DataPage>::SetValue(uint32 inIndex, const M6ValueType& inValue)
{
	assert(inIndex < mPageData.mN);
	mPageData.mData[kM6DataCount - inIndex - 1] = inValue;
}

template<class M6DataType, class M6DataPage>
inline void M6IndexPageT<M6DataType,M6DataPage>::GetKeyValue(uint32 inIndex, string& outKey, M6ValueType& outValue) const
{
	outKey = GetKey(inIndex);
	outValue = GetValue(inIndex);
}

template<class M6DataType, class M6DataPage>
void M6IndexPageT<M6DataType,M6DataPage>::InsertKeyValue(const string& inKey, const M6ValueType& inValue, uint32 inIndex)
{
	assert(inIndex <= mPageData.mN);
	
	if (inIndex < mPageData.mN)
	{
		void* src = mPageData.mKeys + mKeyOffsets[inIndex];
		void* dst = mPageData.mKeys + mKeyOffsets[inIndex] + inKey.length() + 1;
		
		// shift keys
		memmove(dst, src, mKeyOffsets[mPageData.mN] - mKeyOffsets[inIndex]);
		
		// shift data
		src = mPageData.mData + kM6DataCount - mPageData.mN;
		dst = mPageData.mData + kM6DataCount - mPageData.mN - 1;
		
		memmove(dst, src, (mPageData.mN - inIndex) * sizeof(M6ValueType));
	}
	
	uint8* k = mPageData.mKeys + mKeyOffsets[inIndex];
	*k = static_cast<uint8>(inKey.length());
	memcpy(k + 1, inKey.c_str(), *k);
	mPageData.mData[kM6DataCount - inIndex - 1] = inValue;
	++mPageData.mN;

	assert(mPageData.mN <= kM6EntryCount);

	// update key offsets
	for (uint32 i = inIndex + 1; i <= mPageData.mN; ++i)
		mKeyOffsets[i] = static_cast<uint16>(mKeyOffsets[i - 1] + mPageData.mKeys[mKeyOffsets[i - 1]] + 1);

	mDirty = true;
}

//template<class M6DataType, class M6DataPage>
//bool M6IndexPageT<M6DataType,M6DataPage>::GetNext(uint32& ioPage, uint32& ioIndex, M6Tuple& outTuple) const
//{
//	bool result = false;
//	++ioIndex;
//	if (ioIndex < mPageData.mN)
//	{
//		result = true;
//		GetKeyValue(ioIndex, outTuple.key, outTuple.value);
//	}
//	else if (mPageData.mLink != 0)
//	{
//		ioPage = mPageData.mLink;
//		M6IndexPagePtr next(mIndexImpl.Load(ioPage));
//		ioIndex = 0;
//		next->GetKeyValue(ioIndex, outTuple.key, outTuple.value);
//		result = true;
//	}
//	
//	return result;
//}

template<class M6DataType, class M6DataPage>
void M6IndexPageT<M6DataType,M6DataPage>::EraseEntry(uint32 inIndex)
{
	assert(inIndex < mPageData.mN);
	assert(mPageData.mN <= kM6EntryCount);
	
	if (mPageData.mN > 1)
	{
		void* src = mPageData.mKeys + mKeyOffsets[inIndex + 1];
		void* dst = mPageData.mKeys + mKeyOffsets[inIndex];
		uint32 n = mKeyOffsets[mPageData.mN] - mKeyOffsets[inIndex + 1];
		memmove(dst, src, n);
		
		src = mPageData.mData + kM6DataCount - mPageData.mN;
		dst = mPageData.mData + kM6DataCount - mPageData.mN + 1;
		n = (mPageData.mN - inIndex - 1) * sizeof(M6ValueType);
		memmove(dst, src, n);

		for (int i = inIndex + 1; i <= mPageData.mN; ++i)
			mKeyOffsets[i] = mKeyOffsets[i - 1] + mPageData.mKeys[mKeyOffsets[i - 1]] + 1;
	}
	
	--mPageData.mN;
	mDirty = true;
}

template<class M6DataType, class M6DataPage>
void M6IndexPageT<M6DataType,M6DataPage>::ReplaceKey(uint32 inIndex, const string& inKey)
{
	assert(inIndex < mPageData.mN);
	assert(mPageData.mN <= kM6EntryCount);

	uint8* k = mPageData.mKeys + mKeyOffsets[inIndex];
	
	int32 delta = static_cast<int32>(inKey.length()) - *k;
	assert(delta < 0 or Free() >= static_cast<uint32>(delta));
	
	if (inIndex + 1 < mPageData.mN)
	{
		uint8* src = k + *k + 1;
		uint8* dst = src + delta;
		uint32 n = mKeyOffsets[mPageData.mN] - mKeyOffsets[inIndex + 1];
		memmove(dst, src, n);
	}
	
	*k = static_cast<uint8>(inKey.length());
	memcpy(k + 1, inKey.c_str(), inKey.length());

	for (int i = inIndex + 1; i <= mPageData.mN; ++i)
		mKeyOffsets[i] += delta;
	
	mDirty = true;
}

// move entries (keys and data) taking into account insertions and such
template<class M6DataType,class M6DataPage>
void M6IndexPageT<M6DataType,M6DataPage>::MoveEntries(M6IndexPageT& inSrc, M6IndexPageT& inDst,
	uint32 inSrcOffset, uint32 inDstOffset, uint32 inCount)
{
	assert(inSrcOffset <= inSrc.mPageData.mN);
	assert(inDstOffset <= inDst.mPageData.mN);
	assert(inDstOffset + inCount <= kM6MaxEntriesPerPage);
	
	// make room in dst first
	if (inDstOffset < inDst.mPageData.mN)
	{
		// make room in dst by shifting entries
		void* src = inDst.mPageData.mKeys + inDst.mKeyOffsets[inDstOffset];
		void* dst = inDst.mPageData.mKeys + inDst.mKeyOffsets[inDstOffset] +
			inSrc.mKeyOffsets[inSrcOffset + inCount] - inSrc.mKeyOffsets[inSrcOffset];
		uint32 n = inDst.mKeyOffsets[inDst.mPageData.mN] - inDst.mKeyOffsets[inDstOffset];
		memmove(dst, src, n);
		
		src = inDst.mPageData.mData + kM6DataCount - inDst.mPageData.mN;
		dst = inDst.mPageData.mData + kM6DataCount - inDst.mPageData.mN - inCount;
		memmove(dst, src, (inDst.mPageData.mN - inDstOffset) * sizeof(M6ValueType));
	}
	
	// copy keys
	void* src = inSrc.mPageData.mKeys + inSrc.mKeyOffsets[inSrcOffset];
	void* dst = inDst.mPageData.mKeys + inDst.mKeyOffsets[inDstOffset];
	
	uint32 byteCount = inSrc.mKeyOffsets[inSrcOffset + inCount] -
					   inSrc.mKeyOffsets[inSrcOffset];

	assert(inSrc.mKeyOffsets[inSrcOffset] + byteCount <= kM6KeySpace);
	assert(byteCount + inCount * sizeof(M6ValueType) <= inDst.Free());

	memcpy(dst, src, byteCount);
	
	// and data	
	src = inSrc.mPageData.mData + kM6DataCount - inSrcOffset - inCount;
	dst = inDst.mPageData.mData + kM6DataCount - inDstOffset - inCount;
	byteCount = inCount * sizeof(M6ValueType);
	memcpy(dst, src, byteCount);
	
	// and finally move remaining data in src
	if (inSrcOffset + inCount < inSrc.mPageData.mN)
	{
		void* src = inSrc.mPageData.mKeys + inSrc.mKeyOffsets[inSrcOffset + inCount];
		void* dst = inSrc.mPageData.mKeys + inSrc.mKeyOffsets[inSrcOffset];
		uint32 n = inSrc.mKeyOffsets[inSrc.mPageData.mN] - inSrc.mKeyOffsets[inSrcOffset + inCount];
		memmove(dst, src, n);
		
		src = inSrc.mPageData.mData + kM6DataCount - inSrc.mPageData.mN;
		dst = inSrc.mPageData.mData + kM6DataCount - inSrc.mPageData.mN + inCount;
		memmove(dst, src, (inSrc.mPageData.mN - inSrcOffset - inCount) * sizeof(M6ValueType));
	}
	
	inDst.mPageData.mN += inCount;
	inSrc.mPageData.mN -= inCount;
	
	// update key offsets
	uint8* key = inSrc.mPageData.mKeys + inSrc.mKeyOffsets[inSrcOffset];
	for (int32 i = inSrcOffset; i <= inSrc.mPageData.mN; ++i)
	{
		inSrc.mKeyOffsets[i] = static_cast<uint16>(key - inSrc.mPageData.mKeys);
		key += *key + 1;
	}

	key = inDst.mPageData.mKeys + inDst.mKeyOffsets[inDstOffset];
	for (int32 i = inDstOffset; i <= inDst.mPageData.mN; ++i)
	{
		inDst.mKeyOffsets[i] = static_cast<uint16>(key - inDst.mPageData.mKeys);
		key += *key + 1;
	}

	inSrc.mDirty = true;
	inDst.mDirty = true;
}

// --------------------------------------------------------------------

template<class M6DataType>
class M6IndexBranchPage : public M6IndexPageT<M6DataType,M6IndexBranchPageData>
{
  public:
					M6IndexBranchPage(M6IndexImplType& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
						: M6IndexPageT(inIndexImpl, inData, inPageNr)
					{
						mPageData.mType = eM6IndexBranchPage;
					}

	virtual bool	Find(const string& inKey, M6DataType& outValue);
	virtual bool	Insert(string& ioKey, const M6DataType& inValue, uint32& outLink, M6IndexBranchPageType* inParent);
	virtual bool	Erase(string& ioKey, int32 inIndex, M6IndexBranchPageType* inParent, M6IndexBranchPageType* inLinkPage, uint32 inLinkIndex);

  protected:

	virtual bool	Underflow(M6IndexPageType& inRight, uint32 inIndex, M6IndexBranchPageType* inParent);

#if DEBUG
	virtual void	Dump(int inLevel, M6IndexBranchPageType* inParent);
	virtual void	Validate(const string& inKey, M6IndexBranchPageType* inParent);
#endif
};

// --------------------------------------------------------------------

template<class M6DataType>
bool M6IndexBranchPage<M6DataType>::Find(const string& inKey, M6DataType& outValue)
{
	bool match;
	int32 ix;
	
	BinarySearch(inKey, ix, match);

	uint32 pageNr;
	
	if (ix < 0)
		pageNr = GetLink();
	else
		pageNr = GetValue(ix);
	
	M6IndexPagePtr page(mIndexImpl.Load(pageNr));
	bool result = page->Find(inKey, outValue);
	Release(page);
	return result;
}

/*
	Insert returns a bool indicating the depth increased.
	In that case the ioKey and ioValue are updated to the values
	to be inserted in the calling page (or a new root has to be made).
*/

template<class M6DataType>
bool M6IndexBranchPage<M6DataType>::Insert(string& ioKey, const M6DataType& inValue, uint32& outLink, M6IndexBranchPageType* inParent)
{
	bool result = false, match;
	int32 ix;

	BinarySearch(ioKey, ix, match);
	
	uint32 pageNr;
	
	if (ix < 0)
		pageNr = GetLink();
	else
		pageNr = GetValue(ix);

	M6IndexPagePtr page(mIndexImpl.Load(pageNr));
	
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

			M6BranchPagePtr next(mIndexImpl.AllocateBranch());
			
			int32 split = mPageData.mN / 2;
			string upKey;
			uint32 downPage;

			if (ix == split)
			{
				upKey = ioKey;
				downPage = link;

				MoveEntries(*this, *next, split, 0, mPageData.mN - split);
			}
			else if (ix < split)
			{
				--split;
				GetKeyValue(split, upKey, downPage);
				MoveEntries(*this, *next, split + 1, 0, mPageData.mN - split - 1);
				mPageData.mN -= 1;

				if (ix <= split)
					InsertKeyValue(ioKey, link, ix);
				else
					next->InsertKeyValue(ioKey, link, ix - split - 1);
			}
			else
			{
				upKey = GetKey(split);
				downPage = GetValue(split);

				MoveEntries(*this, *next, split + 1, 0, mPageData.mN - split - 1);
				mPageData.mN -= 1;

				if (ix < split)
					InsertKeyValue(ioKey, link, ix);
				else
					next->InsertKeyValue(ioKey, link, ix - split - 1);
			}

			next->SetLink(downPage);
			
			ioKey = upKey;
			outLink = next->GetPageNr();
			
			Release(next);
			
			result = true;
		}
	}

//	assert(mPageData.mN >= kM6MinEntriesPerPage or inParent == nullptr);
	assert(mPageData.mN <= kM6MaxEntriesPerPage);

	Release(page);

	return result;
}

template<class M6DataType>
bool M6IndexBranchPage<M6DataType>::Erase(string& ioKey, int32 inIndex, M6IndexBranchPageType* inParent, M6IndexBranchPageType* inLinkPage, uint32 inLinkIndex)
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
		pageNr = GetLink();
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
				Release(right);
			}
			
			if (TooSmall() and inIndex >= 0)
			{
				// if still too small, try with the left sibling
				M6IndexPagePtr left(mIndexImpl.Load(inIndex > 0 ? inParent->GetValue(inIndex - 1) : inParent->GetLink()));
				left->Underflow(*this, inIndex, inParent);
				Release(left);
			}
		}
	}
	
	Release(page);

	return result;
}

template<class M6DataType>
bool M6IndexBranchPage<M6DataType>::Underflow(M6IndexPageType& inRight, uint32 inIndex, M6IndexBranchPageType* inParent)
{
	M6IndexBranchPageType& right(static_cast<M6IndexBranchPageType&>(inRight));

	// This page left of inRight contains too few entries, see if we can fix this
	// first try a merge

	// pKey is the key in inParent at inIndex (and, since this a leaf, the first key in inRight)
	string pKey = inParent->GetKey(inIndex);
	int32 pKeyLen = static_cast<int32>(pKey.length());

	if (Free() + inRight.Free() - pKeyLen - sizeof(M6DataType) >= kM6KeySpace and
		mPageData.mN + right.mPageData.mN + 1 <= kM6MaxEntriesPerPage)
	{
		InsertKeyValue(pKey, inRight.GetLink(), mPageData.mN);
		
		// join the pages
		MoveEntries(right, *this, 0, mPageData.mN, right.mPageData.mN);
	
		inParent->EraseEntry(inIndex);
		inRight.Deallocate();
	}
	else		// redistribute the data
	{
		if (Free() > inRight.Free() and mPageData.mN < kM6EntryCount)	// rotate an entry from right to left
		{									// but only if it fits in the parent
			string rKey = right.GetKey(0);
			int32 delta = static_cast<int32>(rKey.length() - pKey.length());
			if (delta <= static_cast<int32>(inParent->Free()))
			{
				InsertKeyValue(pKey, right.mPageData.mLink, mPageData.mN);
				inParent->ReplaceKey(inIndex, rKey);
				inParent->SetValue(inIndex, right.mPageNr);
				right.mPageData.mLink = right.GetValue(0);
				right.EraseEntry(0);
			}
		}
		else if (right.Free() > Free() and right.mPageData.mN < kM6EntryCount)
		{
			string lKey = GetKey(mPageData.mN - 1);
			int32 delta = static_cast<int32>(lKey.length() - pKey.length());
			if (delta <= static_cast<int32>(inParent->Free()))
			{
				right.InsertKeyValue(pKey, right.mPageData.mLink, 0);
				right.mPageData.mLink = GetValue(mPageData.mN - 1);
				inParent->ReplaceKey(inIndex, lKey);
				EraseEntry(mPageData.mN - 1);
			}
		}
	}
	
	return not (TooSmall() or inRight.TooSmall());
}

// --------------------------------------------------------------------

template<class M6DataType>
class M6IndexLeafPage : public M6IndexPageT<M6DataType,typename M6IndexPageDataTypeFactory<M6DataType>::M6LeafPageDataType>
{
  public:
	typedef typename M6IndexPageDataTypeFactory<M6DataType>::M6LeafPageDataType	M6DataPageType;
//	typedef typename M6IndexLeafPage*							M6LeafPagePtr;

					M6IndexLeafPage(M6IndexImplType& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
						: M6IndexPageT(inIndexImpl, inData, inPageNr)
					{
						mPageData.mType = M6DataPageType::kIndexPageType;
					}

	virtual bool	Find(const string& inKey, M6DataType& outValue);
	virtual bool	Insert(string& ioKey, const M6DataType& inValue, uint32& outLink, M6IndexBranchPageType* inParent);
	virtual bool	Erase(string& ioKey, int32 inIndex, M6IndexBranchPageType* inParent, M6IndexBranchPageType* inLinkPage, uint32 inLinkIndex);

	virtual bool	Underflow(M6IndexPageType& inRight, uint32 inIndex, M6IndexBranchPageType* inParent);

#if DEBUG
	virtual void	Dump(int inLevel, M6IndexBranchPageType* inParent);
	virtual void	Validate(const string& inKey, M6IndexBranchPageType* inParent);
#endif
};

// --------------------------------------------------------------------

template<class M6DataType>
bool M6IndexLeafPage<M6DataType>::Find(const string& inKey, M6DataType& outValue)
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

template<class M6DataType>
bool M6IndexLeafPage<M6DataType>::Insert(string& ioKey, const M6DataType& inValue, uint32& outLink, M6IndexBranchPageType* inParent)
{
	bool result = false;

	int32 ix;
	bool match;
	BinarySearch(ioKey, ix, match);
	
	if (match)
		SetValue(ix, inValue);	// simply update the value (we're a unique index)
	else if (CanStore(ioKey))
		InsertKeyValue(ioKey, inValue, ix + 1);
	else
	{
		ix += 1;	// we need to insert at ix + 1

		// create a new leaf page
		M6LeafPagePtr next(mIndexImpl.AllocateLeaf());
	
		uint32 split = mPageData.mN / 2;

		MoveEntries(*this, *next, split, 0, mPageData.mN - split);
		next->SetLink(GetLink());
		SetLink(next->GetPageNr());
		
		if (ix <= mPageData.mN)
			InsertKeyValue(ioKey, inValue, ix);
		else
			next->InsertKeyValue(ioKey, inValue, ix - mPageData.mN);
		
		ioKey = next->GetKey(0);
		outLink = next->GetPageNr();
		
		Release(next);
		
		result = true;
	}

//	assert(mPageData.mN >= kM6MinEntriesPerPage or inParent == nullptr);
	assert(mPageData.mN <= kM6MaxEntriesPerPage);

	return result;
}

template<class M6DataType>
bool M6IndexLeafPage<M6DataType>::Erase(string& ioKey, int32 inIndex, M6IndexBranchPageType* inParent, M6IndexBranchPageType* inLinkPage, uint32 inLinkIndex)
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
			assert(mPageData.mN > 0);
			if (ix == 0 and mPageData.mN > 0 and inLinkPage != nullptr)	// need to pass on the new key
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
					Release(right);
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
					Release(left);
				}
				
				result = true;
			}
		}
	}
	
	return result;
}

template<class M6DataType>
bool M6IndexLeafPage<M6DataType>::Underflow(M6IndexPageType& inRight, uint32 inIndex, M6IndexBranchPageType* inParent)
{
	M6IndexLeafPage& right(dynamic_cast<M6IndexLeafPage&>(inRight));
	
	// Page left of right contains too few entries, see if we can fix this
	// first try a merge
	if (Free() + right.Free() >= kM6KeySpace and
		mPageData.mN + right.mPageData.mN <= kM6MaxEntriesPerPage)
	{
		// join the pages
		MoveEntries(right, *this, 0, mPageData.mN, right.mPageData.mN);
		SetLink(right.GetLink());
	
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
		
		if (Free() > right.Free() and mPageData.mN < kM6EntryCount)	// move items from right to left
		{
			assert(TooSmall());

			int32 delta = Free() - right.Free();
			int32 needed = delta / 2;
			
			uint8* rk = right.mPageData.mKeys;
			uint32 n = 0, ln = 0;
			while (n < right.mPageData.mN and n + mPageData.mN < kM6EntryCount and needed > *rk)
			{
				++n;
				if ((*rk - pKeyLen + pFree) > 0)	// if the new first key of right fits in the parent
					ln = n;							// we have a candidate
				needed -= *rk + sizeof(M6DataType);
				rk += *rk + 1;
			}
			
			// move the data
			MoveEntries(right, *this, 0, mPageData.mN, ln);
			inParent->ReplaceKey(inIndex, right.GetKey(0));
		}
		else if (right.Free() > Free() and right.mPageData.mN < kM6EntryCount)
		{
			assert(right.TooSmall());

			int32 delta = right.Free() - Free();
			int32 needed = delta / 2;
			
			uint8* rk = mPageData.mKeys + mKeyOffsets[mPageData.mN - 1];
			uint32 n = 0, ln = 0;
			while (n < mPageData.mN and n + right.mPageData.mN < kM6EntryCount and needed > *rk)
			{
				++n;
				if ((*rk - pKeyLen + pFree) > 0)	// if the new first key of right fits in the parent
					ln = n;							// we have a candidate
				needed -= *rk + sizeof(M6DataType);
				rk = mPageData.mKeys + mKeyOffsets[mPageData.mN - 1 - n];
			}
			
			// move the data
			MoveEntries(*this, right, mPageData.mN - ln, 0, ln);
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
	if (inMode == eReadWrite and mFile.Size() == 0)
	{
		M6IxFileHeaderPage page = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
		mFile.PWrite(&page, kM6IndexPageSize, 0);

		mHeader = page.mHeader;
		mHeader.mDepth = 0;
		mDirty = true;
		
		mFile.PWrite(mHeader, 0);
	}
	else
		mFile.PRead(mHeader, 0);
	
	assert(mHeader.mSignature == kM6IndexFileSignature);
	assert(mHeader.mHeaderSize == sizeof(M6IxFileHeader));
}

//M6IndexImpl::M6IndexImpl(M6BasicIndex& inIndex, const string& inPath,
//		M6SortedInputIterator& inData)
//	: mFile(inPath, eReadWrite)
//	, mIndex(inIndex)
//	, mDirty(false)
//	, mAutoCommit(true)
//{
//	InitCache();	
//
//	mFile.Truncate(0);
//	
//	M6IxFileHeaderPage data = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
//	mFile.PWrite(&data, kM6IndexPageSize, 0);
//
//	mHeader = data.mHeader;
//	mFile.PWrite(mHeader, 0);
//	
//	string last;
//	
//	try
//	{
//		// inData is sorted, so we start by writing out leaf pages:
//		M6IndexPagePtr page(Allocate<M6IndexLeafPage>());
//		
//		deque<M6Tuple> up;		// keep track of the nodes we have to create for the next levels
//		M6Tuple tuple;
//		
//		tuple.value = page->GetPageNr();
//		up.push_back(tuple);
//		
//		while (inData(tuple))
//		{
//			// sanity check
//			if (CompareKeys(last, tuple.key) >= 0)
//				THROW(("Trying to build index from unsorted data"));
//			
//			if (not page->CanStore(tuple.key))
//			{
//				M6IndexPagePtr next(Allocate<M6IndexLeafPage>());
//				page->SetLink(next->GetPageNr());
//				page.reset(next.release());
//	
//				up.push_back(tuple);
//				up.back().value = page->GetPageNr();
//			}
//			
//			page->InsertKeyValue(tuple.key, tuple.value, page->GetN());
//			++mHeader.mSize;
//		}
//		
//		// all data is written in the leafs, now construct branch pages
//		CreateUpLevels(up);
//	}
//	catch (...)
//	{
//		Rollback();
//		throw;
//	}
//
//	Commit();
//}

M6IndexImpl::~M6IndexImpl()
{
	if (mDirty)
		mFile.PWrite(mHeader, 0);
}

void M6IndexImpl::SetAutoCommit(bool inAutoCommit)
{
	mAutoCommit = inAutoCommit;
}

// --------------------------------------------------------------------

template<class M6DataType>
M6IndexImplT<M6DataType>::M6IndexImplT(M6BasicIndex& inIndex, const string& inPath, MOpenMode inMode)
	: M6IndexImpl(inIndex, inPath, inMode)
{
	InitCache();
}

template<class M6DataType>
M6IndexImplT<M6DataType>::~M6IndexImplT()
{
	delete[] mCache;
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::InitCache()
{
	mCacheCount = kM6CacheCount;
	mCache = new M6CachedPage[mCacheCount];
	
	for (uint32 ix = 0; ix < mCacheCount; ++ix)
	{
		mCache[ix].mPageNr = 0;
		mCache[ix].mRefCount = 0;
		mCache[ix].mNext = mCache + ix + 1;
		mCache[ix].mPrev = mCache + ix - 1;
	}
	
	mCache[0].mPrev = mCache[mCacheCount - 1].mNext = nullptr;
	mLRUHead = mCache;
	mLRUTail = mCache + mCacheCount - 1;
}

template<class M6DataType>
typename M6IndexImplT<M6DataType>::M6CachedPagePtr M6IndexImplT<M6DataType>::GetCachePage(uint32 inPageNr)
{
	M6CachedPagePtr result = mLRUTail;
	
	// now search backwards for a cached page that can be recycled
	uint32 n = 0;
	while (result != nullptr and result->mPage and 
		(result->mRefCount > 1 or result->mPage->IsDirty()))
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
			b->mRefCount = a->mRefCount;
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
	
	if (result->mPage != nullptr)
		delete result->mPage;
	result->mPage = nullptr;
	
	return result;
}

template<class M6DataType>
typename M6IndexImplT<M6DataType>::M6LeafPagePtr M6IndexImplT<M6DataType>::AllocateLeaf()
{
	int64 fileSize = mFile.Size();
	uint32 pageNr = static_cast<uint32>((fileSize - 1) / kM6IndexPageSize + 1);
	int64 offset = pageNr * kM6IndexPageSize;
	mFile.Truncate(offset + kM6IndexPageSize);
	
	M6IndexPageData* data = new M6IndexPageData;
	memset(data, 0, kM6IndexPageSize);
	
	M6CachedPagePtr cp = GetCachePage(pageNr);
	M6LeafPagePtr page(new M6LeafPageType(*this, data, pageNr));
	cp->mPage = page;
	cp->mRefCount = 1;
	return page;
}

template<class M6DataType>
typename M6IndexImplT<M6DataType>::M6BranchPagePtr M6IndexImplT<M6DataType>::AllocateBranch()
{
	int64 fileSize = mFile.Size();
	uint32 pageNr = static_cast<uint32>((fileSize - 1) / kM6IndexPageSize + 1);
	int64 offset = pageNr * kM6IndexPageSize;
	mFile.Truncate(offset + kM6IndexPageSize);
	
	M6IndexPageData* data = new M6IndexPageData;
	memset(data, 0, kM6IndexPageSize);
	
	M6CachedPagePtr cp = GetCachePage(pageNr);
	M6BranchPagePtr page(new M6BranchPageType(*this, data, pageNr));
	cp->mPage = page;
	cp->mRefCount = 1;
	return page;
}

template<class M6DataType>
typename M6IndexImplT<M6DataType>::M6IndexPagePtr M6IndexImplT<M6DataType>::Load(uint32 inPageNr)
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
		mFile.PRead(data, kM6IndexPageSize, inPageNr * kM6IndexPageSize);

		if (data->leaf.mType == eM6IndexBranchPage)
			cp->mPage = new M6BranchPageType(*this, data, inPageNr);
		else
			cp->mPage = new M6LeafPageType(*this, data, inPageNr);
		cp->mRefCount = 1;
	}
	
	return cp->mPage;
}

template<class M6DataType>
typename void M6IndexImplT<M6DataType>::Release(M6IndexPageType*& inPage)
{
	assert(inPage != nullptr);
	
	M6CachedPagePtr cp = mLRUHead;
	while (cp != nullptr and cp->mPage != inPage)
		cp = cp->mNext;
	
	if (cp == nullptr)
		THROW(("Invalid page in Release"));
	
	cp->mRefCount -= 1;
	
	inPage = nullptr;
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Commit()
{
	for (uint32 ix = 0; ix < mCacheCount; ++ix)
	{
		if (mCache[ix].mPage and mCache[ix].mPage->IsDirty())
			mCache[ix].mPage->Flush(mFile);
	}
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Rollback()
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

template<class M6DataType>
void M6IndexImplT<M6DataType>::Insert(const string& inKey, const M6DataType& inValue)
{
	try
	{
		if (mHeader.mRoot == 0)	// empty index?
		{
			M6LeafPagePtr root(AllocateLeaf());
			mHeader.mRoot = root->GetPageNr();
			mHeader.mDepth = 1;
			Release(root);
		}
		
		M6IndexPagePtr root(Load(mHeader.mRoot));
		
		string key(inKey);
		uint32 link;
	
		if (root->Insert(key, inValue, link, nullptr))
		{
			// increase depth
			++mHeader.mDepth;
	
			M6BranchPagePtr newRoot(AllocateBranch());
			newRoot->SetLink(mHeader.mRoot);
			newRoot->InsertKeyValue(key, link, 0);
			mHeader.mRoot = newRoot->GetPageNr();
			Release(newRoot);
		}
		
		Release(root);
	
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

template<class M6DataType>
bool M6IndexImplT<M6DataType>::Erase(const string& inKey)
{
	if (mHeader.mRoot == 0)
		return false;
	
	bool result = false;
	
	try
	{
		M6IndexPagePtr root(Load(mHeader.mRoot));
		
		string key(inKey);
	
		if (root->Erase(key, 0, nullptr, nullptr, 0))
		{
			result = true;
			
			if (root->GetN() == 0)
			{
				mHeader.mRoot = root->GetLink();
				root->Deallocate();
				--mHeader.mDepth;
			}
		}
		
		Release(root);
		
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

template<class M6DataType>
bool M6IndexImplT<M6DataType>::Find(const string& inKey, M6DataType& outValue)
{
	bool result = false;
	if (mHeader.mRoot != 0)
	{
		M6IndexPagePtr root(Load(mHeader.mRoot));
		result = root->Find(inKey, outValue);
		Release(root);
	}
	return result;
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Vacuum()
{
	//try
	//{
	//	// start by locating the first leaf node.
	//	// Then compact the nodes and shift them to the start
	//	// of the file. Truncate the file and reconstruct the
	//	// branch nodes.
	//
	//	uint32 pageNr = mHeader.mRoot;
	//	for (;;)
	//	{
	//		M6IndexPagePtr page(Load(pageNr));
	//		if (page->IsLeaf())
	//			break;
	//		pageNr = page->GetLink();
	//	}
	//
	//	// keep an indirect array of reordered pages
	//	size_t pageCount = (mFile.Size() / kM6IndexPageSize) + 1;
	//	vector<uint32> ix1(pageCount);
	//	iota(ix1.begin(), ix1.end(), 0);
	//	vector<uint32> ix2(ix1);

	//	deque<M6Tuple> up;
	//	uint32 n = 1;

	//	for (;;)
	//	{
	//		pageNr = ix1[pageNr];
	//		M6IndexPagePtr page(Load(pageNr));
	//		if (pageNr != n)
	//		{
	//			swap(ix1[ix2[pageNr]], ix1[ix2[n]]);
	//			swap(ix2[pageNr], ix2[n]);
	//			page->MoveTo(n);
	//		}

	//		up.push_back(M6Tuple(page->GetKey(0), page->GetPageNr()));
	//		uint32 link = page->GetLink();
	//	
	//		while (link != 0)
	//		{
	//			M6IndexPagePtr next(Load(ix1[link]));
	//			if (next->GetN() == 0)
	//			{
	//				link = next->GetLink();
	//				continue;
	//			}

	//			string key = next->GetKey(0);
	//			assert(key.compare(page->GetKey(page->GetN() - 1)) > 0);
	//			if (not page->CanStore(key))
	//				break;
	//		
	//			page->InsertKeyValue(key, next->GetValue(0), page->GetN());
	//			next->EraseEntry(0);
	//		}
	//	
	//		if (link == 0)
	//		{
	//			page->SetLink(0);
	//			break;
	//		}

	//		pageNr = link;

	//		++n;
	//		page->SetLink(n);
	//	}
	//
	//	// OK, so we have all the pages on disk, in order.
	//	// truncate the file (erasing the remaining pages)
	//	// and rebuild the branches.
	//	mFile.Truncate((n + 1) * kM6IndexPageSize);
	//
	//	CreateUpLevels(up);

	//	Commit();
	//}
	//catch (...)
	//{
	//	Rollback();
	//	throw;
	//}
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::CreateUpLevels(deque<pair<string,M6DataType>>& up)
{
	//mHeader.mDepth = 1;
	//while (up.size() > 1)
	//{
	//	++mHeader.mDepth;

	//	deque<M6Tuple> nextUp;
	//	
	//	// we have at least two tuples, so take the first and use it as
	//	// link, and the second as first entry for the first page
	//	M6Tuple tuple = up.front();
	//	up.pop_front();
	//	
	//	M6IndexPagePtr page(AllocateBranch());
	//	page->SetLink(tuple.value);
	//	
	//	// store this new page for the next round
	//	nextUp.push_back(M6Tuple(tuple.key, page->GetPageNr()));
	//	
	//	while (not up.empty())
	//	{
	//		tuple = up.front();
	//		
	//		// make sure we never end up with an empty page
	//		if (page->CanStore(tuple.key))
	//		{
	//			if (up.size() == 1)
	//			{
	//				page->InsertKeyValue(tuple.key, tuple.value, page->GetN());
	//				up.pop_front();
	//				break;
	//			}
	//			
	//			// special case, if up.size() == 2 and we can store both
	//			// keys, store them and break the loop
	//			if (up.size() == 2 and
	//				page->GetN() + 1 < kM6MaxEntriesPerPage and
	//				page->Free() >= (up[0].key.length() + up[1].key.length() + 2 + 2 * sizeof(M6DataType)))
	//			{
	//				page->InsertKeyValue(up[0].key, up[0].value, page->GetN());
	//				page->InsertKeyValue(up[1].key, up[1].value, page->GetN());
	//				break;
	//			}
	//			
	//			// otherwise, only store the key if there's enough
	//			// in up to avoid an empty page
	//			if (up.size() > 2)
	//			{
	//				page->InsertKeyValue(tuple.key, tuple.value, page->GetN());
	//				up.pop_front();
	//				continue;
	//			}
	//		}

	//		// cannot store the tuple, create new page
	//		page = Allocate<M6IndexBranchPage>();
	//		page->SetLink(tuple.value);

	//		nextUp.push_back(M6Tuple(tuple.key, page->GetPageNr()));
	//		up.pop_front();
	//	}
	//	
	//	up = nextUp;
	//}
	//
	//assert(up.size() == 1);
	//mHeader.mRoot = up.front().value;
	//mDirty = true;
}

//M6BasicIndex::iterator M6IndexImpl::Begin()
//{
//	uint32 pageNr = mHeader.mRoot;
//	for (;;)
//	{
//		M6IndexPagePtr page(Load(pageNr));
//		if (page->IsLeaf())
//			break;
//		pageNr = page->GetLink();
//	}
//	
//	return M6BasicIndex::iterator(this, pageNr, 0);
//}
//
//M6BasicIndex::iterator M6IndexImpl::End()
//{
//	return M6BasicIndex::iterator(nullptr, 0, 0);
//}
//
//// --------------------------------------------------------------------
//
//M6BasicIndex::iterator::iterator()
//	: mIndex(nullptr)
//	, mPage(0)
//	, mKeyNr(0)
//{
//}
//
//M6BasicIndex::iterator::iterator(const iterator& iter)
//	: mIndex(iter.mIndex)
//	, mPage(iter.mPage)
//	, mKeyNr(iter.mKeyNr)
//	, mCurrent(iter.mCurrent)
//{
//}
//
//M6BasicIndex::iterator::iterator(M6IndexImpl* inImpl, uint32 inPageNr, uint32 inKeyNr)
//	: mIndex(inImpl)
//	, mPage(inPageNr)
//	, mKeyNr(inKeyNr)
//{
//	if (mIndex != nullptr)
//	{
//		M6IndexPagePtr page(mIndex->Load(mPage));
//		page->GetKeyValue(mKeyNr, mCurrent.key, mCurrent.value);
//	}
//}
//
//M6BasicIndex::iterator& M6BasicIndex::iterator::operator=(const iterator& iter)
//{
//	if (this != &iter)
//	{
//		mIndex = iter.mIndex;
//		mPage = iter.mPage;
//		mKeyNr = iter.mKeyNr;
//		mCurrent = iter.mCurrent;
//	}
//	
//	return *this;
//}
//
//M6BasicIndex::iterator& M6BasicIndex::iterator::operator++()
//{
//	M6IndexPagePtr page(mIndex->Load(mPage));
//	if (not page->GetNext(mPage, mKeyNr, mCurrent))
//	{
//		mIndex = nullptr;
//		mPage = 0;
//		mKeyNr = 0;
//		mCurrent = M6Tuple();
//	}
//
//	return *this;
//}

// --------------------------------------------------------------------

M6BasicIndex::M6BasicIndex(const string& inPath, MOpenMode inMode)
	: mImpl(new M6IndexImplT<uint32>(*this, inPath, inMode))
{
}

//M6BasicIndex::M6BasicIndex(const string& inPath, M6SortedInputIterator& inData)
//	: mImpl(new M6IndexImplT<uint32>(*this, inPath, inData))
//{
//}

M6BasicIndex::M6BasicIndex(M6IndexImpl* inImpl)
	: mImpl(inImpl)
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

//M6BasicIndex::iterator M6BasicIndex::begin() const
//{
//	return mImpl->Begin();
//}
//
//M6BasicIndex::iterator M6BasicIndex::end() const
//{
//	return mImpl->End();
//}

void M6BasicIndex::Insert(const string& key, uint32 value)
{
	if (key.length() >= kM6MaxKeyLength)
		THROW(("Invalid key length"));

	static_cast<M6IndexImplT<uint32>*>(mImpl)->Insert(key, value);
}

void M6BasicIndex::Erase(const string& key)
{
	static_cast<M6IndexImplT<uint32>*>(mImpl)->Erase(key);
}

bool M6BasicIndex::Find(const string& inKey, uint32& outValue)
{
	return static_cast<M6IndexImplT<uint32>*>(mImpl)->Find(inKey, outValue);
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
					M6ValidationException(uint32 inPageNr, const char* inReason)
						: mPageNr(inPageNr)
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

#define M6VALID_ASSERT(cond)	do { if (not (cond)) throw M6ValidationException(GetPageNr(), #cond ); } while (false)

template<class M6DataType>
void M6IndexLeafPage<M6DataType>::Validate(const string& inKey, M6IndexBranchPageType* inParent)
{
//	M6VALID_ASSERT(mPageData.mN >= kM6MinEntriesPerPage or inParent == nullptr);
	//M6VALID_ASSERT(inParent == nullptr or not TooSmall());
	M6VALID_ASSERT(inKey.empty() or GetKey(0) == inKey);
	
	for (uint32 i = 0; i < mPageData.mN; ++i)
	{
		if (i > 0)
		{
//			IM6VALID_ASSERT(GetValue(i) > GetValue(i - 1));
			M6VALID_ASSERT(mIndexImpl.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
		}
	}
	
	if (mPageData.mLink != 0)
	{
		M6IndexPagePtr next(mIndexImpl.Load(mPageData.mLink));
		//M6VALID_ASSERT(mIndexImpl.CompareKeys(GetKey(mPageData.mN - 1), next->GetKey(0)) < 0);
		Release(next);
	}
}

template<class M6DataType>
void M6IndexBranchPage<M6DataType>::Validate(const string& inKey, M6IndexBranchPageType* inParent)
{
//		M6VALID_ASSERT(mPageData.mN >= kM6MinEntriesPerPage or inParent == nullptr);
	//M6VALID_ASSERT(inParent == nullptr or not TooSmall());
//		M6VALID_ASSERT(mPageData.mN <= kM6MaxEntriesPerPage);

	for (uint32 i = 0; i < mPageData.mN; ++i)
	{
		M6IndexPagePtr link(mIndexImpl.Load(mPageData.mLink));
		link->Validate(inKey, this);
		Release(link);
		
		for (uint32 i = 0; i < mPageData.mN; ++i)
		{
			M6IndexPagePtr page(mIndexImpl.Load(GetValue(i)));
			page->Validate(GetKey(i), this);
			Release(page);
			if (i > 0)
				M6VALID_ASSERT(mIndexImpl.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
		}
	}
}

template<class M6DataType>
void M6IndexLeafPage<M6DataType>::Dump(int inLevel, M6IndexBranchPageType* inParent)
{
	string prefix(inLevel * 2, ' ');

	cout << prefix << "leaf page at " << mPageNr << "; N = " << mPageData.mN << ": [";
	for (int i = 0; i < mPageData.mN; ++i)
		cout << GetKey(i) << '(' << GetValue(i) << ')'
			 << (i + 1 < mPageData.mN ? ", " : "");
	cout << "]" << endl;

	if (mPageData.mLink)
	{
		M6IndexPagePtr next(mIndexImpl.Load(mPageData.mLink));
		//cout << prefix << "  " << "link: " << next->GetKey(0) << endl;
		Release(next);
	}
}

template<class M6DataType>
void M6IndexBranchPage<M6DataType>::Dump(int inLevel, M6IndexBranchPageType* inParent)
{
	string prefix(inLevel * 2, ' ');

	cout << prefix << (inParent ? "branch" : "root") << " page at " << mPageNr << "; N = " << mPageData.mN << ": {";
	for (int i = 0; i < mPageData.mN; ++i)
		cout << GetKey(i) << (i + 1 < mPageData.mN ? ", " : "");
	cout << "}" << endl;

	M6IndexPagePtr link(mIndexImpl.Load(mPageData.mLink));
	link->Dump(inLevel + 1, this);
	Release(link);
	
	for (int i = 0; i < mPageData.mN; ++i)
	{
		cout << prefix << inLevel << '.' << i << ") " << GetKey(i) << endl;
		
		M6IndexPagePtr sub(mIndexImpl.Load(GetValue(i)));
		sub->Dump(inLevel + 1, this);
		Release(sub);
	}
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Validate()
{
	try
	{
		M6IndexPagePtr root(Load(mHeader.mRoot));
		root->Validate("", nullptr);
		Release(root);
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

template<class M6DataType>
void M6IndexImplT<M6DataType>::Dump()
{
	cout << endl
		 << "Dumping tree" << endl
		 << endl;

	M6IndexPagePtr root(Load(mHeader.mRoot));
	root->Dump(0, nullptr);
	Release(root);
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
	: M6BasicIndex(new M6IndexImplT<M6MultiData>(*this, inPath, inMode))
{
}

void M6MultiBasicIndex::Insert(const string& inKey, const vector<uint32>& inDocuments)
{
	M6MultiData data = { inDocuments.size(), inDocuments.front() };
	static_cast<M6IndexImplT<M6MultiData>*>(mImpl)->Insert(inKey, data);
}

bool M6MultiBasicIndex::Find(const string& inKey, iterator& outIterator)
{
	return false;
}

// --------------------------------------------------------------------

M6MultiIDLBasicIndex::M6MultiIDLBasicIndex(const string& inPath, MOpenMode inMode)
	: M6BasicIndex(new M6IndexImplT<M6MultiIDLData>(*this, inPath, inMode))
{
}

void M6MultiIDLBasicIndex::Insert(const string& inKey, int64 inIDLOffset, const vector<uint32>& inDocuments)
{
	M6MultiIDLData data = { inDocuments.size(), 0, inIDLOffset };
	static_cast<M6IndexImplT<M6MultiIDLData>*>(mImpl)->Insert(inKey, data);
}

bool M6MultiIDLBasicIndex::Find(const string& inKey, multi_iterator& outIterator, int64& outIDLOffset)
{
	return false;
}

// --------------------------------------------------------------------

M6WeightedBasicIndex::M6WeightedBasicIndex(const string& inPath, MOpenMode inMode)
	: M6BasicIndex(new M6IndexImplT<M6MultiData>(*this, inPath, inMode))
{
}

void M6WeightedBasicIndex::Insert(const string& inKey, const vector<pair<uint32,uint8>>& inDocuments)
{
	M6MultiData data = { inDocuments.size() };
	static_cast<M6IndexImplT<M6MultiData>*>(mImpl)->Insert(inKey, data);
}

bool M6WeightedBasicIndex::Find(const string& inKey, weighted_iterator& outIterator)
{
	return false;
}
