#include "M6Lib.h"

#include <deque>
#include <vector>
#include <list>
#include <numeric>
#include <memory>

#include <boost/static_assert.hpp>
#include <boost/tr1/tuple.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/lexical_cast.hpp>

#include "M6Index.h"
#include "M6Error.h"

// --------------------------------------------------------------------

using namespace std;
using namespace std::tr1;

// --------------------------------------------------------------------

// The index will probably never have keys less than 3 bytes in length.
// Including the length byte, this means a minimal key length of 4. Add
// the data int64 and the minimal storage per entry is 12 bytes.
// Given this, the maximum number of keys we will ever store in a page
// is (pageSize - headerSize) / 12. For a 512 byte page and 8 byte header
// this boils down to 42.

const uint32
//	kM6IndexPageSize = 8192,
	kM6IndexPageSize = 128,
	kM6IndexPageHeaderSize = 8,
//	kM6MaxEntriesPerPage = (kM6IndexPageSize - kM6IndexPageHeaderSize) / 12,	// keeps code simple
	kM6MaxEntriesPerPage = 4,
	kM6MinEntriesPerPage = 2,
	kM6IndexPageKeySpace = kM6IndexPageSize - kM6IndexPageHeaderSize,
	kM6IndexPageMinKeySpace = kM6IndexPageKeySpace / 4,
	kM6MaxKeyLength = (255 < kM6IndexPageMinKeySpace ? 255 : kM6IndexPageMinKeySpace),
	kM6IndexPageDataCount = (kM6IndexPageKeySpace / sizeof(int64));

BOOST_STATIC_ASSERT(kM6IndexPageDataCount >= kM6MaxEntriesPerPage);

enum {
	eM6IndexPageIsEmpty		= (1 << 0),		// deallocated page
	eM6IndexPageIsLeaf		= (1 << 1),		// leaf page in B+ tree
	eM6IndexPageIsBits		= (1 << 2),		// page containing bit streams
	eM6IndexPageIsDirty		= (1 << 3),		// page is modified, needs to be written
	eM6IndexPageLocked		= (1 << 4),		// page is in use
};

struct M6IndexPageData
{
	uint16		mFlags;
	uint16		mN;
	uint32		mLink;		// Used to link leaf pages or to store page[0]
	union
	{
		uint8	mKeys[kM6IndexPageKeySpace];
		int64	mData[kM6IndexPageDataCount];
	};

	template<class Archive>
	void serialize(Archive& ar)
	{
		ar & mFlags & mN & mLink & mKeys;
	}
};

BOOST_STATIC_ASSERT(sizeof(M6IndexPageData) == kM6IndexPageSize);

const uint32
//	kM6IndexFileSignature	= FOUR_CHAR_INLINE('m6ix');
	kM6IndexFileSignature	= 'm6ix';

struct M6IxFileHeader
{
	uint32		mSignature;
	uint32		mHeaderSize;
	uint32		mSize;
	uint32		mRoot;
	uint32		mDepth;

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

class M6IndexImpl
{
  public:
	typedef M6BasicIndex::iterator	iterator;

					M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, MOpenMode inMode);
					M6IndexImpl(M6BasicIndex& inIndex, const string& inPath,
						M6SortedInputIterator& inData);
					~M6IndexImpl();

	void			Insert(const string& inKey, int64 inValue);
	void			Erase(const string& inKey);
	bool			Find(const string& inKey, int64& outValue);
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

	// page cache
	void			AllocatePage(uint32& outPageNr, M6IndexPageData*& outData);
	void			CachePage(uint32 inPageNr, M6IndexPageData*& outData);
	void			ReleasePage(uint32 inPageNr, M6IndexPageData*& inData);
	void			SwapPages(uint32 inPageA, uint32 inPageB);
	
  private:

	void			CreateUpLevels(deque<M6Tuple>& up);

	M6File			mFile;
	M6BasicIndex&	mIndex;
	M6IxFileHeader	mHeader;
	bool			mDirty;

	static const uint32 kM6LRUCacheSize = 10;
	struct M6LRUCacheEntry
	{
		uint32				mPageNr;
		M6IndexPageData*	mData;
	};
	
	list<M6LRUCacheEntry>	mCache;
};

// --------------------------------------------------------------------

class M6IndexPage
{
  public:
					M6IndexPage(M6IndexImpl& inIndex, bool inIsLeaf);
					M6IndexPage(M6IndexImpl& inIndex, uint32 inPageNr);

					// special constructor, splits off the right half of inLeft
					M6IndexPage(M6IndexPage& inLeft, uint32 inAtIndex);

					~M6IndexPage();

	void			AllocateNew(bool inLinkNewToOld = false);
	void			Deallocate();
	
	bool			IsLeaf() const					{ return mData->mFlags & eM6IndexPageIsLeaf; }
	void			SetLeaf(bool isLeaf);
	
	uint32			GetN() const					{ return mData->mN; }

	uint32			Free() const					{ return kM6IndexPageKeySpace - mKeyOffsets[mData->mN] - mData->mN * sizeof(int64); }
	bool			CanStore(const string& inKey)	{ return mData->mN < kM6MaxEntriesPerPage and Free() >= inKey.length() + 1 + sizeof(int64); }
	bool			TooSmall() const
					{
#if DEBUG
						return mData->mN < kM6MinEntriesPerPage;
#else

#endif
					}
	
	void			SetLink(int64 inLink);
	uint32			GetLink() const					{ return mData->mLink; }
	
	void			MoveTo(uint32 inPageNr);
	
	bool			Insert(string& ioKey, int64& ioValue);
	bool			Erase(string& ioKey, M6IndexPage* inParent, int32 inIndex);

	bool			Find(const string& inKey, int64& outValue);
	
	uint32			GetPageNr() const				{ return mPageNr; }
	
	int32			BinarySearch(const string& inKey) const;

	void			GetTuple(uint32 inIndex, M6Tuple& outTuple) const;
	string			GetKey(uint32 inIndex) const;
	int64			GetValue(uint32 inIndex) const;
	uint32			GetValue32(uint32 inIndex) const;

	void			InsertKeyValue(const string& inKey, int64 inValue, uint32 inIndex);
	void			Erase(uint32 inIndex);
	void			ReplaceKey(uint32 inIndex, const string& inKey);

	bool			GetNext(uint32& ioPage, uint32& ioKey, M6Tuple& outTuple) const;

#if DEBUG
	void			Validate(const string& inKey);
	void			Dump(int inLevel = 0);
#endif

  private:

					M6IndexPage(const M6IndexPage&);
	M6IndexPage&	operator=(const M6IndexPage&);

	static void		MoveEntries(M6IndexPage& inFrom, M6IndexPage& inTo,
						uint32 inFromOffset, uint32 inToOffset, uint32 inCount);

	static bool		UnderflowLeaf(M6IndexPage& inParent, M6IndexPage& inLeft, M6IndexPage& inRight, uint32 inIndex);
	static bool		UnderflowBranch(M6IndexPage& inParent, M6IndexPage& inLeft, M6IndexPage& inRight, uint32 inIndex);
	void			JoinLeaf(M6IndexPage& inRight, M6IndexPage& inParent, uint32 inIndex);
	void			JoinBranch(M6IndexPage& inRight, M6IndexPage& inParent, uint32 inIndex);

	M6IndexImpl&	mIndexImpl;
	M6IndexPageData*
					mData;
	uint16			mKeyOffsets[kM6MaxEntriesPerPage + 1];
	uint32			mPageNr;
};

M6IndexPage::M6IndexPage(M6IndexImpl& inIndex, bool inIsLeaf)
	: mIndexImpl(inIndex)
	, mData(nullptr)
{
	AllocateNew();
	
	// by default we create leaf pages.
	if (inIsLeaf)
		mData->mFlags |= eM6IndexPageIsLeaf;
	mData->mFlags |= eM6IndexPageIsDirty;
}

M6IndexPage::M6IndexPage(M6IndexImpl& inIndex, uint32 inPageNr)
	: mIndexImpl(inIndex)
	, mData(nullptr)
	, mPageNr(inPageNr)
{
	if (mPageNr == 0)
		THROW(("Invalid page number"));
	
	mIndexImpl.CachePage(mPageNr, mData);
	assert(mData->mFlags & eM6IndexPageLocked);
	
	assert(mData->mN <= kM6MaxEntriesPerPage);
	
	uint8* key = mData->mKeys;
	for (uint32 i = 0; i <= mData->mN; ++i)
	{
		assert(key <= mData->mKeys + kM6IndexPageSize);
		mKeyOffsets[i] = static_cast<uint16>(key - mData->mKeys);
		key += *key + 1;
	}
}

M6IndexPage::M6IndexPage(M6IndexPage& inLeft, uint32 inOffset)
	: mIndexImpl(inLeft.mIndexImpl)
	, mData(nullptr)
{
	AllocateNew();
	assert(mData->mFlags & eM6IndexPageLocked);
	assert(mData->mFlags & eM6IndexPageIsDirty);
	
	// copy leaf flag
	if (inLeft.mData->mFlags & eM6IndexPageIsLeaf)
		mData->mFlags |= eM6IndexPageIsLeaf;

	uint32 rlink = inLeft.GetValue32(inOffset - 1);
	
	// leaf nodes are split differently from branch nodes
	MoveEntries(inLeft, *this, inOffset, 0, inLeft.mData->mN - inOffset);
	
	// update rest
	mData->mFlags = inLeft.mData->mFlags;
	
	if (IsLeaf())
	{
		mData->mLink = inLeft.mData->mLink;
		inLeft.mData->mLink = GetPageNr();
	}
	else
	{
		mData->mLink = static_cast<uint32>(swap_bytes(inLeft.mData->mData[kM6IndexPageDataCount - inOffset]));
		inLeft.mData->mN = inOffset - 1;
	}
}

M6IndexPage::~M6IndexPage()
{
	mIndexImpl.ReleasePage(mPageNr, mData);
}

void M6IndexPage::AllocateNew(bool inLinkNewToOld)
{
	bool isLeaf = false;
	
	M6IndexPageData* newData;
	uint32 newPageNr;
	mIndexImpl.AllocatePage(newPageNr, newData);
	
	if (inLinkNewToOld)
	{
		mData->mLink = newPageNr;
		mData->mFlags |= eM6IndexPageIsDirty;
	}
	
	if (mData != nullptr)
	{
		isLeaf = mData->mFlags & eM6IndexPageIsLeaf;
		mIndexImpl.ReleasePage(mPageNr, mData);
	}

	mData = newData;
	mPageNr = newPageNr;
	assert(mData->mFlags & eM6IndexPageLocked);
	assert(mData->mFlags & eM6IndexPageIsDirty);
	
	if (isLeaf)
		mData->mFlags |= eM6IndexPageIsLeaf;
	
	mKeyOffsets[0] = 0;
}

void M6IndexPage::Deallocate()
{
	static const M6IndexPageData kEmpty = { eM6IndexPageIsEmpty | eM6IndexPageIsDirty };
	*mData = kEmpty;
}

// move entries (keys and data) taking into account insertions and such
void M6IndexPage::MoveEntries(M6IndexPage& inSrc, M6IndexPage& inDst,
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
		void* dst = inDst.mData->mKeys + inDst.mKeyOffsets[inDstOffset + inCount];
		uint32 n = inDst.mKeyOffsets[inDst.mData->mN] - inDst.mKeyOffsets[inDstOffset];
		memmove(dst, src, n);
		
		src = inDst.mData->mData + kM6IndexPageDataCount - inDst.mData->mN;
		dst = inDst.mData->mData + kM6IndexPageDataCount - inDst.mData->mN - inCount;
		memmove(dst, src, (inDst.mData->mN - inDstOffset) * sizeof(int64));
	}
	
	// copy keys
	void* src = inSrc.mData->mKeys + inSrc.mKeyOffsets[inSrcOffset];
	void* dst = inDst.mData->mKeys + inDst.mKeyOffsets[inDstOffset];
	
	uint32 byteCount = inSrc.mKeyOffsets[inSrcOffset + inCount] -
					   inSrc.mKeyOffsets[inSrcOffset];

	assert(inSrc.mKeyOffsets[inSrcOffset] + byteCount <= kM6IndexPageKeySpace);
	assert(byteCount + inCount * sizeof(int64) <= inDst.Free());

	memcpy(dst, src, byteCount);
	
	// and data	
	src = inSrc.mData->mData + kM6IndexPageDataCount - inSrcOffset - inCount;
	dst = inDst.mData->mData + kM6IndexPageDataCount - inDstOffset - inCount;
	byteCount = inCount * sizeof(int64);
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
		memmove(dst, src, (inSrc.mData->mN - inSrcOffset - inCount) * sizeof(int64));
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

	inSrc.mData->mFlags |= eM6IndexPageIsDirty;
	inDst.mData->mFlags |= eM6IndexPageIsDirty;
}

void M6IndexPage::JoinLeaf(M6IndexPage& inRight, M6IndexPage& inParent, uint32 inIndex)
{
	MoveEntries(inRight, *this, 0, mData->mN, inRight.mData->mN);
	
	if (IsLeaf())
		mData->mLink = inRight.mData->mLink;

	inParent.Erase(inIndex);
	inRight.Deallocate();
	
	mData->mFlags |= eM6IndexPageIsDirty;
}

bool M6IndexPage::UnderflowLeaf(M6IndexPage& inParent, M6IndexPage& inLeft, M6IndexPage& inRight, uint32 inIndex)
{
	// Page left of right contains too few entries, see if we can fix this
	// first try a merge
	if (inLeft.Free() + inRight.Free() >= kM6IndexPageKeySpace and
		inLeft.mData->mN + inRight.mData->mN <= kM6MaxEntriesPerPage)
	{
		inLeft.JoinLeaf(inRight, inParent, inIndex);
	}
	else		// redistribute the data
	{
		// pKey is the key in inParent at inIndex (and, since this a leaf, the first key in inRight)
		string pKey = inParent.GetKey(inIndex);
		assert(pKey == inRight.GetKey(0));
		int32 pKeyLen = static_cast<int32>(pKey.length());
		int32 pFree = inParent.Free();
		
		if (inLeft.Free() > inRight.Free())	// move items from right to left
		{
			assert(inLeft.TooSmall());

			int32 delta = inLeft.Free() - inRight.Free();
			int32 needed = delta / 2;
			
			uint8* rk = inRight.mData->mKeys;
			uint32 n = 0, ln = 0;
			while (n < inRight.mData->mN and n + inLeft.mData->mN < kM6IndexPageDataCount and needed > *rk)
			{
				++n;
				if ((*rk - pKeyLen + pFree) > 0)	// if the new first key of right fits in the parent
					ln = n;							// we have a candidate
				needed -= *rk + sizeof(int64);
				rk += *rk + 1;
			}
			
			// move the data
			MoveEntries(inRight, inLeft, 0, inLeft.mData->mN, ln);
			inParent.ReplaceKey(inIndex, inRight.GetKey(0));
		}
		else
		{
			assert(inRight.TooSmall());

			int32 delta = inRight.Free() - inLeft.Free();
			int32 needed = delta / 2;
			
			uint8* rk = inLeft.mData->mKeys + inLeft.mKeyOffsets[inLeft.mData->mN - 1];
			uint32 n = 0, ln = 0;
			while (n < inLeft.mData->mN and n + inRight.mData->mN < kM6IndexPageDataCount and needed > *rk)
			{
				++n;
				if ((*rk - pKeyLen + pFree) > 0)	// if the new first key of right fits in the parent
					ln = n;							// we have a candidate
				needed -= *rk + sizeof(int64);
				rk = inLeft.mData->mKeys + inLeft.mKeyOffsets[inLeft.mData->mN - 1 - n];
			}
			
			// move the data
			MoveEntries(inLeft, inRight, inLeft.mData->mN - ln, 0, ln);
			inParent.ReplaceKey(inIndex, inRight.GetKey(0));
		}
	}
	
	return not (inLeft.TooSmall() or inRight.TooSmall());
}

bool M6IndexPage::UnderflowBranch(M6IndexPage& inParent, M6IndexPage& inLeft, M6IndexPage& inRight, uint32 inIndex)
{
//	// Page left of right contains too few entries, see if we can fix this
//	// first try a merge
//	if (inLeft.Free() + inRight.Free() >= kM6IndexPageKeySpace and
//		inLeft.mData->mN + inRight.mData->mN <= kM6MaxEntriesPerPage)
//	{
//		inLeft.JoinBranch(inRight, inParent, inIndex);
//	}
//	else		// redistribute the data
//	{
//		// pKey is the key in inParent at inIndex (and, since this a leaf, the first key in inRight)
//		string pKey = inParent.GetKey(inIndex);
//		assert(pKey == inRight.GetKey(0));
//		int32 pKeyLen = static_cast<int32>(pKey.length());
//		int32 pFree = inParent.Free();
//		
//		if (inLeft.Free() > inRight.Free())	// move items from right to left
//		{
//			assert(inLeft.TooSmall());
//
//			int32 delta = inLeft.Free() - inRight.Free();
//			int32 needed = delta / 2;
//			
//			uint8* rk = inRight.mData->mKeys;
//			uint32 n = 0, ln = 0;
//			while (n < inRight.mData->mN and n + inLeft.mData->mN < kM6IndexPageDataCount and needed > *rk)
//			{
//				++n;
//				if ((*rk - pKeyLen + pFree) > 0)	// if the new first key of right fits in the parent
//					ln = n;							// we have a candidate
//				needed -= *rk + sizeof(int64);
//				rk += *rk + 1;
//			}
//			
//			// move the data
//			MoveEntries(inRight, inLeft, 0, inLeft.mData->mN, ln);
//			inParent.ReplaceKey(inIndex, inRight.GetKey(0));
//		}
//		else
//		{
//			assert(inRight.TooSmall());
//
//			int32 delta = inRight.Free() - inLeft.Free();
//			int32 needed = delta / 2;
//			
//			uint8* rk = inLeft.mData->mKeys + inLeft.mKeyOffsets[inLeft.mData->mN - 1];
//			uint32 n = 0, ln = 0;
//			while (n < inLeft.mData->mN and n + inRight.mData->mN < kM6IndexPageDataCount and needed > *rk)
//			{
//				++n;
//				if ((*rk - pKeyLen + pFree) > 0)	// if the new first key of right fits in the parent
//					ln = n;							// we have a candidate
//				needed -= *rk + sizeof(int64);
//				rk = inLeft.mData->mKeys + inLeft.mKeyOffsets[inLeft.mData->mN - 1 - n];
//			}
//			
//			// move the data
//			MoveEntries(inLeft, inRight, inLeft.mData->mN - ln, 0, ln);
//			inParent.ReplaceKey(inIndex, inRight.GetKey(0));
//		}
//	}
	
	return not (inLeft.TooSmall() or inRight.TooSmall());
}

void M6IndexPage::SetLink(int64 inLink)
{
	if (inLink > numeric_limits<uint32>::max())
		THROW(("Invalid link value"));
	
	mData->mLink = static_cast<uint32>(inLink);
	mData->mFlags |= eM6IndexPageIsDirty;
}

void M6IndexPage::MoveTo(uint32 inPageNr)
{
	if (inPageNr != mPageNr)
	{
		M6IndexPage page(mIndexImpl, inPageNr);

		mIndexImpl.SwapPages(mPageNr, inPageNr);

		page.mPageNr = mPageNr;
		if (page.IsLeaf())	// only save page if it is a leaf
			page.mData->mFlags |= eM6IndexPageIsDirty;
		
		mPageNr = inPageNr;
		mData->mFlags |= eM6IndexPageIsDirty;
	}
}

void M6IndexPage::SetLeaf(bool inIsLeaf)
{
	if (inIsLeaf)
		mData->mFlags |= eM6IndexPageIsLeaf;
	else
		mData->mFlags &= ~eM6IndexPageIsLeaf;
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

inline uint32 M6IndexPage::GetValue32(uint32 inIndex) const
{
	assert(inIndex < mData->mN);
	int64 v = swap_bytes(mData->mData[kM6IndexPageDataCount - inIndex - 1]);
	if (v > numeric_limits<uint32>::max())
		THROW(("Invalid value"));
	return static_cast<uint32>(v);
}

inline void M6IndexPage::GetTuple(uint32 inIndex, M6Tuple& outTuple) const
{
	outTuple.key = GetKey(inIndex);
	outTuple.value = GetValue(inIndex);
}

bool M6IndexPage::GetNext(uint32& ioPage, uint32& ioIndex, M6Tuple& outTuple) const
{
	bool result = false;
	++ioIndex;
	if (ioIndex < mData->mN)
	{
		result = true;
		GetTuple(ioIndex, outTuple);
	}
	else if (mData->mLink != 0)
	{
		ioPage = mData->mLink;
		M6IndexPage next(mIndexImpl, ioPage);
		ioIndex = 0;
		next.GetTuple(ioIndex, outTuple);
		result = true;
	}
	
	return result;
}

int32 M6IndexPage::BinarySearch(const string& inKey) const
{
	int32 L = 0, R = mData->mN - 1;
	while (L <= R)
	{
		int32 i = (L + R) / 2;

		const uint8* ko = mData->mKeys + mKeyOffsets[i];
		const char* k = reinterpret_cast<const char*>(ko + 1);

		int d = mIndexImpl.CompareKeys(inKey.c_str(), inKey.length(), k, *ko);
		if (d < 0)
			R = i - 1;
		else
			L = i + 1;
	}
	
	return R;
}

/*
	Insert returns a bool indicating the depth increased.
	In that case the ioKey and ioValue are updated to the values
	to be inserted in the calling page (or a new root has to be made).
*/

bool M6IndexPage::Insert(string& ioKey, int64& ioValue)
{
	bool result = false;

	// Start by locating a position in the page
	int32 L = 0, R = mData->mN - 1;
	while (L <= R)
	{
		int32 i = (L + R) / 2;

		const uint8* ko = mData->mKeys + mKeyOffsets[i];
		const char* k = reinterpret_cast<const char*>(ko + 1);

		int d = mIndexImpl.CompareKeys(ioKey.c_str(), ioKey.length(), k, *ko);
		if (d < 0)
			R = i - 1;
		else
			L = i + 1;
	}

	// R now points to the first key greater or equal to ioKey
	// or it is -1 one if all keys are larger than ioKey
	uint32 ix = R + 1;
	
	if (IsLeaf())
	{
		if (CanStore(ioKey))
			InsertKeyValue(ioKey, ioValue, R + 1);
		else
		{
			// create a new leaf page
			uint32 split = mData->mN / 2 + 1;
			M6IndexPage next(*this, split);
			
			if (ix <= mData->mN)
				InsertKeyValue(ioKey, ioValue, ix);
			else
				next.InsertKeyValue(ioKey, ioValue, ix - mData->mN);
			
			ioKey = next.GetKey(0);
			ioValue = next.GetPageNr();
			
			result = true;
		}
	}
	else
	{
		uint32 pageNr;
		
		if (R < 0)
			pageNr = mData->mLink;
		else
			pageNr = GetValue32(R);

		assert(pageNr);
		
		M6IndexPage page(mIndexImpl, pageNr);
		if (page.Insert(ioKey, ioValue))
		{
			// page was split, we now need to store ioKey in our page

			if (CanStore(ioKey))
				InsertKeyValue(ioKey, ioValue, ix);
			else
			{
				// the key needs to be inserted but it didn't fit
				// so we need to split the page
				
				uint32 split = mData->mN / 2 + 1;
				string up = GetKey(split);
				
				M6IndexPage next(*this, split + 1);
				
				if (ix <= mData->mN)
					InsertKeyValue(ioKey, ioValue, ix);
				else
					next.InsertKeyValue(ioKey, ioValue, ix - mData->mN - 1);
				
				ioKey = up;
				ioValue = next.GetPageNr();
				
				result = true;
			}
		}
	}

	return result;
}

void M6IndexPage::Erase(uint32 inIndex)
{
	assert(inIndex < mData->mN);
	
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

		for (int i = inIndex + 1; i < mData->mN; ++i)
			mKeyOffsets[i] = mKeyOffsets[i - 1] + mData->mKeys[mKeyOffsets[i - 1]] + 1;
	}
	
	--mData->mN;
	mData->mFlags |= eM6IndexPageIsDirty;
}

void M6IndexPage::ReplaceKey(uint32 inIndex, const string& inKey)
{
	assert(inIndex < mData->mN);

	uint8* k = mData->mKeys + mKeyOffsets[inIndex];
	
	int32 delta = static_cast<int32>(inKey.length()) - *k;
	assert(delta < 0 or Free() >= static_cast<uint32>(delta));
	
	uint8* src = k + *k;
	uint8* dst = src + delta;
	uint32 n = mKeyOffsets[mData->mN] - mKeyOffsets[inIndex + 1];
	memmove(dst, src, n);
	
	*k = static_cast<uint8>(inKey.length());
	memcpy(k + 1, inKey.c_str(), inKey.length());

	for (int i = inIndex + 1; i < mData->mN; ++i)
		mKeyOffsets[i] += delta;
	
	mData->mFlags |= eM6IndexPageIsDirty;
}

bool M6IndexPage::Erase(string& ioKey, M6IndexPage* inParent, int32 inIndex)
{
	bool result = false, match = false;
	
	// Start by locating a position in the page
	int L = 0, R = mData->mN - 1;
	while (L <= R and not match)
	{
		int i = (L + R) / 2;

		const uint8* ko = mData->mKeys + mKeyOffsets[i];
		const char* k = reinterpret_cast<const char*>(ko + 1);

		int d = mIndexImpl.CompareKeys(ioKey.c_str(), ioKey.length(), k, *ko);
		if (d == 0)
		{
			match = true;
			R = i;
		}
		else if (d < 0)
			R = i - 1;
		else
			L = i + 1;
	}

	// R now points to the first key greater or equal to inKey
	// or it is -1 one if all keys are larger than inKey

	if (not IsLeaf())
	{
		// if this is a non-leaf page, propagate the Erase
		uint32 pageNr;
		
		if (R < 0)
			pageNr = mData->mLink;
		else
			pageNr = GetValue32(R);
		
		M6IndexPage page(mIndexImpl, pageNr);
		if (page.Erase(ioKey, this, R))
		{
			if (TooSmall() and inParent != nullptr)
			{
				if (inIndex + 1 < inParent->mData->mN)
				{
					// try to compensate using our right sibling
					M6IndexPage right(mIndexImpl, inParent->GetValue32(inIndex + 1));
					UnderflowBranch(*inParent, *this, right, inIndex + 1);
				}
				
				if (TooSmall() and inIndex >= 0)
				{
					// if still too small, try with the left sibling
					M6IndexPage left(mIndexImpl, inIndex > 0 ? inParent->GetValue32(inIndex - 1) : inParent->GetLink());
					UnderflowBranch(*inParent, left, *this, inIndex);
				}
			}
		}

		if (match)
		{
			string key = GetKey(R);
			if (ioKey.length() > key.length() and Free() < ioKey.length() - key.length())
				THROW(("Need to implement"));
			ReplaceKey(R, ioKey);
		}
	}
	else if (match)		// match in a leaf page
	{
		// erase the key at R
		Erase(R);
		
		if (R == 0 and inParent != nullptr)	// need to pass on the new key
		{
			ioKey.clear();
			if (mData->mN > 0)
				ioKey = GetKey(0);
			else if (mData->mLink != 0)
			{
				M6IndexPage link(mIndexImpl, mData->mLink);
				assert(link.mData->mN);
				ioKey = link.GetKey(0);
			}
		}
		
		if (inParent != nullptr and TooSmall())
		{							// we're not the root page and we've lost too many entries
			if (inIndex + 1 < inParent->mData->mN)
			{
				// try to compensate using our right sibling
				M6IndexPage right(mIndexImpl, inParent->GetValue32(inIndex + 1));
				UnderflowLeaf(*inParent, *this, right, inIndex + 1);
			}
			
			if (TooSmall() and inIndex >= 0)
			{
				// if still too small, try with the left sibling
				M6IndexPage left(mIndexImpl, inIndex > 0 ? inParent->GetValue32(inIndex - 1) : inParent->GetLink());
				UnderflowLeaf(*inParent, left, *this, inIndex);
			}
			
			result = true;
		}
	}
	
	return result;
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

	// update key offsets
	for (uint32 i = inIndex + 1; i <= mData->mN; ++i)
		mKeyOffsets[i] = static_cast<uint16>(mKeyOffsets[i - 1] + mData->mKeys[mKeyOffsets[i - 1]] + 1);

	mData->mFlags |= eM6IndexPageIsDirty;
}

bool M6IndexPage::Find(const string& inKey, int64& outValue)
{
	bool result = false;

	// Start by locating a position in the page
	int L = 0, R = mData->mN - 1;
	while (L <= R)
	{
		int i = (L + R) / 2;

		const uint8* ko = mData->mKeys + mKeyOffsets[i];
		const char* k = reinterpret_cast<const char*>(ko + 1);

		int d = mIndexImpl.CompareKeys(inKey.c_str(), inKey.length(), k, *ko);
		
		// found ?
		if (d == 0 and IsLeaf())
		{
			outValue = GetValue(i);
			result = true;
			break;
		}
		else if (d < 0)
			R = i - 1;
		else
			L = i + 1;
	}
	
	if (not IsLeaf())
	{
		uint32 pageNr;
		
		if (R < 0)
			pageNr = mData->mLink;
		else
			pageNr = GetValue32(R);
		
		M6IndexPage page(mIndexImpl, pageNr);
		result = page.Find(inKey, outValue);
	}
	
	return result;
}

// --------------------------------------------------------------------

M6IndexImpl::M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, MOpenMode inMode)
	: mFile(inPath, inMode)
	, mIndex(inIndex)
	, mDirty(false)
{
	if (inMode == eReadWrite and mFile.Size() == 0)
	{
		M6IxFileHeaderPage page = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
		mFile.PWrite(&page, kM6IndexPageSize, 0);

		// and create a root page to keep code simple
		M6IndexPage root(*this, true);

		mHeader = page.mHeader;
		mHeader.mRoot = root.GetPageNr();
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
{
	mFile.Truncate(0);
	
	M6IxFileHeaderPage data = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
	mFile.PWrite(&data, kM6IndexPageSize, 0);

	mHeader = data.mHeader;
	mFile.PWrite(mHeader, 0);
	
	// inData is sorted, so we start by writing out leaf pages:
	M6IndexPage page(*this, true);
	
	deque<M6Tuple> up;		// keep track of the nodes we have to create for the next levels
	M6Tuple tuple;
	
	tuple.value = page.GetPageNr();
	up.push_back(tuple);
	
	while (inData(tuple))
	{
		if (not page.CanStore(tuple.key))
		{
			page.AllocateNew(true);
			up.push_back(tuple);
			up.back().value = page.GetPageNr();
		}
		
		page.InsertKeyValue(tuple.key, tuple.value, page.GetN());
		++mHeader.mSize;
	}
	
	// all data is written in the leafs, now construct branch pages
	CreateUpLevels(up);
}

M6IndexImpl::~M6IndexImpl()
{
	if (mDirty)
		mFile.PWrite(mHeader, 0);

	for (auto c = mCache.begin(); c != mCache.end(); ++c)
	{
		if (c->mData->mFlags & eM6IndexPageIsDirty)
			THROW(("Page still dirty!"));

		if (c->mData->mFlags & eM6IndexPageLocked)
			THROW(("Page still locked!"));
		
		delete c->mData;
	}
}

void M6IndexImpl::AllocatePage(uint32& outPageNr, M6IndexPageData*& ioData)
{
	int64 fileSize = mFile.Size();
	outPageNr = static_cast<uint32>((fileSize - 1) / kM6IndexPageSize) + 1;
	int64 offset = outPageNr * kM6IndexPageSize;
	mFile.Truncate(offset + kM6IndexPageSize);
	
	M6LRUCacheEntry e;
	e.mPageNr = outPageNr;
	e.mData = ioData = new M6IndexPageData;
	mCache.push_front(e);
	
	memset(ioData, 0, kM6IndexPageSize);
	
	ioData->mFlags |= eM6IndexPageLocked | eM6IndexPageIsDirty;
}

void M6IndexImpl::CachePage(uint32 inPageNr, M6IndexPageData*& ioData)
{
	if (inPageNr == 0)
		THROW(("Invalid page number"));
	
	ioData = nullptr;
	
	foreach (M6LRUCacheEntry& e, mCache)
	{
		if (e.mPageNr == inPageNr)
		{
			ioData = e.mData;
			if (ioData->mFlags & eM6IndexPageLocked)
				THROW(("Internal error, cache page is locked"));
			break;
		}
	}
	
	if (ioData == nullptr)
	{
		M6LRUCacheEntry e;
		e.mPageNr = inPageNr;
		e.mData = ioData = new M6IndexPageData;
		mCache.push_front(e);
		
		mFile.PRead(*ioData, inPageNr * kM6IndexPageSize);
	}

	ioData->mFlags |= eM6IndexPageLocked;
}

void M6IndexImpl::ReleasePage(uint32 inPageNr, M6IndexPageData*& ioData)
{
	// validation
	bool found = false;
	foreach (M6LRUCacheEntry& e, mCache)
	{
		if (e.mData == ioData and e.mPageNr == inPageNr)
		{
			found = true;
			break;
		}
	}
	
	if (not found)
		THROW(("Invalid page in release"));
	
	if (ioData->mFlags & eM6IndexPageIsDirty)
	{
		ioData->mFlags &= ~(eM6IndexPageIsDirty|eM6IndexPageLocked);
		mFile.PWrite(*ioData, inPageNr * kM6IndexPageSize);
	}
	else
		ioData->mFlags &= ~eM6IndexPageLocked;

	ioData = nullptr;

	// clean up the cache, if needed
	auto c = mCache.end();
	while (c != mCache.begin() and mCache.size() > kM6LRUCacheSize)
	{
		--c;
		if ((c->mData->mFlags & eM6IndexPageLocked) == 0)
		{
			delete c->mData;
			c = mCache.erase(c);
		}
	}
}

void M6IndexImpl::SwapPages(uint32 inPageA, uint32 inPageB)
{
	list<M6LRUCacheEntry>::iterator a = mCache.end(), b = mCache.end(), i;
	for (i = mCache.begin(); i != mCache.end(); ++i)
	{
		if (i->mPageNr == inPageA)
			a = i;
		if (i->mPageNr == inPageB)
			b = i;
	}
	
	if (a == mCache.end() or b == mCache.end())
		THROW(("Invalid pages in SwapPages"));
	
	swap(a->mPageNr, b->mPageNr);
}

void M6IndexImpl::Insert(const string& inKey, int64 inValue)
{
	M6IndexPage root(*this, mHeader.mRoot);
	
	string key(inKey);
	int64 value(inValue);

	if (root.Insert(key, value))
	{
		// increase depth
		++mHeader.mDepth;

		M6IndexPage newRoot(*this, false);
		newRoot.SetLink(mHeader.mRoot);
		newRoot.InsertKeyValue(key, value, 0);
		mHeader.mRoot = newRoot.GetPageNr();
	}

	++mHeader.mSize;
	mDirty = true;
}

void M6IndexImpl::Erase(const string& inKey)
{
	M6IndexPage root(*this, mHeader.mRoot);
	
	string key(inKey);

	if (root.Erase(key, nullptr, 0))
	{
		if (root.GetN() == 0 and mHeader.mDepth > 1)
		{
			mHeader.mRoot = root.GetLink();
			root.Deallocate();
			--mHeader.mDepth;
		}
	}

	--mHeader.mSize;
	mDirty = true;
}

bool M6IndexImpl::Find(const string& inKey, int64& outValue)
{
	M6IndexPage root(*this, mHeader.mRoot);
	return root.Find(inKey, outValue);
}

void M6IndexImpl::Vacuum()
{
	// start by locating the first leaf node.
	// Then compact the nodes and shift them to the start
	// of the file. Truncate the file and reconstruct the
	// branch nodes.
	
	uint32 pageNr = mHeader.mRoot;
	for (;;)
	{
		M6IndexPage page(*this, pageNr);
		if (page.IsLeaf())
			break;
		pageNr = page.GetLink();
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
		M6IndexPage page(*this, pageNr);
		if (pageNr != n)
		{
			swap(ix1[ix2[pageNr]], ix1[ix2[n]]);
			swap(ix2[pageNr], ix2[n]);
			page.MoveTo(n);
		}

		up.push_back(M6Tuple(page.GetKey(0), page.GetPageNr()));
		uint32 link = page.GetLink();
		
		while (link != 0)
		{
			M6IndexPage next(*this, ix1[link]);
			if (next.GetN() == 0)
			{
				link = next.GetLink();
				continue;
			}

			string key = next.GetKey(0);
			assert(key.compare(page.GetKey(page.GetN() - 1)) > 0);
			if (not page.CanStore(key))
				break;
			
			page.InsertKeyValue(key, next.GetValue(0), page.GetN());
			next.Erase(0);
		}
		
		if (link == 0)
		{
			page.SetLink(0);
			break;
		}

		pageNr = link;

		++n;
		page.SetLink(n);
	}
	
	// OK, so we have all the pages on disk, in order.
	// truncate the file (erasing the remaining pages)
	// and rebuild the branches.
	mFile.Truncate((n + 1) * kM6IndexPageSize);
	
	CreateUpLevels(up);
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
		
		M6IndexPage page(*this, false);
		page.SetLeaf(false);
		page.SetLink(tuple.value);
		
		// store this new page for the next round
		nextUp.push_back(M6Tuple(tuple.key, page.GetPageNr()));
		
		while (not up.empty())
		{
			tuple = up.front();
			
			// make sure we never end up with an empty page
			if (page.CanStore(tuple.key))
			{
				if (up.size() == 1)
				{
					page.InsertKeyValue(tuple.key, tuple.value, page.GetN());
					up.pop_front();
					break;
				}
				
				// special case, if up.size() == 2 and we can store both
				// keys, store them and break the loop
				if (up.size() == 2 and
					page.GetN() + 1 < kM6MaxEntriesPerPage and
					page.Free() >= (up[0].key.length() + up[1].key.length() + 2 + 2 * sizeof(int64)))
				{
					page.InsertKeyValue(up[0].key, up[0].value, page.GetN());
					page.InsertKeyValue(up[1].key, up[1].value, page.GetN());
					break;
				}
				
				// otherwise, only store the key if there's enough
				// in up to avoid an empty page
				if (up.size() > 2)
				{
					page.InsertKeyValue(tuple.key, tuple.value, page.GetN());
					up.pop_front();
					continue;
				}
			}

			// cannot store the tuple, create new page
			page.AllocateNew();
			page.SetLeaf(false);
			page.SetLink(tuple.value);

			nextUp.push_back(M6Tuple(tuple.key, page.GetPageNr()));
			up.pop_front();
		}
		
		up = nextUp;
	}
	
	assert(up.size() == 1);
	mHeader.mRoot = static_cast<uint32>(up.front().value);
	mDirty = true;
}

M6BasicIndex::iterator M6IndexImpl::Begin()
{
	uint32 pageNr = mHeader.mRoot;
	for (;;)
	{
		M6IndexPage page(*this, pageNr);
		if (page.IsLeaf())
			break;
		pageNr = page.GetLink();
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
		M6IndexPage page(*mIndex, mPage);
		page.GetTuple(mKeyNr, mCurrent);
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
	M6IndexPage page(*mIndex, mPage);
	if (not page.GetNext(mPage, mKeyNr, mCurrent))
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
	mImpl->Dump();
}

void M6BasicIndex::erase(const string& key)
{
	mImpl->Erase(key);
	mImpl->Dump();
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

// DEBUG code

#if DEBUG

void M6IndexPage::Validate(const string& inKey)
{
	if (IsLeaf())
	{
		assert(mData->mN > 0);
		assert(inKey.empty() or GetKey(0) == inKey);
		
		for (uint32 i = 0; i < mData->mN; ++i)
		{
			assert(boost::lexical_cast<int64>(GetKey(i)) == GetValue(i));
			if (i > 0)
			{
				assert(GetValue(i) > GetValue(i - 1));
				assert(mIndexImpl.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
			}
		}
		
		if (mData->mLink != 0)
		{
			M6IndexPage next(mIndexImpl, mData->mLink);
			assert(mIndexImpl.CompareKeys(GetKey(mData->mN - 1), next.GetKey(0)) < 0);
		}
	}
	else
	{
		for (uint32 i = 0; i < mData->mN; ++i)
		{
			M6IndexPage link(mIndexImpl, mData->mLink);
			link.Validate(inKey);
			
			for (uint32 i = 0; i < mData->mN; ++i)
			{
				M6IndexPage page(mIndexImpl, GetValue32(i));
				page.Validate(GetKey(i));
				if (i > 0)
					assert(mIndexImpl.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
			}
		}
	}
}

void M6IndexPage::Dump(int inLevel)
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
			M6IndexPage next(mIndexImpl, mData->mLink);
			cout << prefix << "  " << "link: " << next.GetKey(0) << endl;
		}
	}
	else
	{
		cout << prefix << "branch page at " << mPageNr << "; N = " << mData->mN << ": {";
		for (int i = 0; i < mData->mN; ++i)
			cout << GetKey(i) << (i + 1 < mData->mN ? ", " : "");
		cout << "}" << endl;

		M6IndexPage link(mIndexImpl, mData->mLink);
		link.Dump(inLevel + 1);
		
		for (int i = 0; i < mData->mN; ++i)
		{
			cout << prefix << i << ") " << GetKey(i) << endl;
			
			M6IndexPage sub(mIndexImpl, GetValue32(i));
			sub.Dump(inLevel + 1);
		}
	}
}

void M6IndexImpl::Validate()
{
	M6IndexPage root(*this, mHeader.mRoot);
	root.Validate("");
}

void M6IndexImpl::Dump()
{
	cout << endl
		 << "Dumping tree" << endl
		 << endl;

	M6IndexPage root(*this, mHeader.mRoot);
	root.Dump(0);
	root.Validate("");
}

void M6BasicIndex::dump() const
{
	mIndexImpl->Dump();
}

void M6BasicIndex::validate() const
{
	mIndexImpl->Validate();
}

#endif
