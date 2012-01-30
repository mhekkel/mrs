#include "M6Lib.h"

#include <deque>
#include <vector>
#include <numeric>

#include <boost/static_assert.hpp>

#include "M6Index.h"
#include "M6Error.h"

using namespace std;

// --------------------------------------------------------------------

// The index will probably never have keys less than 3 bytes in length.
// Including the length byte, this means a minimal key length of 4. Add
// the data int64 and the minimal storage per entry is 12 bytes.
// Given this, the maximum number of keys we will ever store in a page
// is (pageSize - headerSize) / 12. For a 512 byte page and 8 byte header
// this boils down to 42.

const uint32
	kM6IndexPageSize = 8192,
//	kM6IndexPageSize = 128,
	kM6IndexPageHeaderSize = 8,
	kM6MaxEntriesPerPage = (kM6IndexPageSize - kM6IndexPageHeaderSize) / 12,	// keeps code simple
//	kM6MaxEntriesPerPage = 4,
	kM6MinEntriesPerPage = 2,
	kM6IndexPageKeySpace = kM6IndexPageSize - kM6IndexPageHeaderSize,
	kM6IndexPageMinKeySpace = kM6IndexPageKeySpace / 2,
	kM6MaxKeyLength = (255 < kM6IndexPageMinKeySpace ? 255 : kM6IndexPageMinKeySpace),
	kM6IndexPageDataCount = (kM6IndexPageKeySpace / sizeof(int64));

BOOST_STATIC_ASSERT(kM6IndexPageDataCount >= kM6MaxEntriesPerPage);

enum {
	eM6IndexPageIsEmpty		= (1 << 0),		// deallocated page
	eM6IndexPageIsLeaf		= (1 << 1),		// leaf page in B+ tree
	eM6IndexPageIsBits		= (1 << 2),		// page containing bit streams
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

class M6IndexPage;
class M6IndexBranchPage;
class M6IndexLeafPage;

class M6IndexPagePtr
{
  public:
					M6IndexPagePtr();
					M6IndexPagePtr(M6IndexPage* inPage);
					M6IndexPagePtr(const M6IndexPagePtr& inPtr);
					~M6IndexPagePtr();

	M6IndexPagePtr&	operator=(const M6IndexPagePtr& inPtr);

	M6IndexPage*	release();
	void			reset(M6IndexPage* inPage);

	M6IndexPage&	operator*() const			{ return *mPage; }
	M6IndexPage*	operator->() const			{ return mPage; }
					operator bool() const		{ return mPage != nullptr; }

  private:
	M6IndexPage*	mPage;	
};

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

	void			Commit();
	void			Rollback();
	void			SetAutoCommit(bool inAutoCommit);

	// page cache
	M6IndexPagePtr	AllocateLeaf(M6IndexBranchPage* inParent);
	M6IndexPagePtr	AllocateBranch(M6IndexBranchPage* inParent);
	void			Deallocate(uint32 inPageNr);
	M6IndexPagePtr	Cache(uint32 inPageNr, M6IndexBranchPage* inParent);
	void			CleanupCache();
	
	M6File&			GetFile()					{ return mFile; }

  private:

	void			CreateUpLevels(deque<M6Tuple>& up);

	M6File			mFile;
	M6BasicIndex&	mIndex;
	M6IxFileHeader	mHeader;
	bool			mDirty;
	bool			mAutoCommit;

	static const uint32 kM6LRUCacheSize = 25;
	M6IndexPage*	mCache;
	uint32			mCachedCount;
};

// --------------------------------------------------------------------

class M6IndexPage
{
  public:
	virtual 		~M6IndexPage();

	void			Deallocate();
	void			Flush();

	void			Reference()						{ ++mRefCount; }
	void			Release()						{ --mRefCount; }
	int32			GetRefCount() const				{ return mRefCount; }
	
	M6IndexPage*	GetNext() const					{ return mNext; }
	void			SetNext(M6IndexPage* inNext)	{ mNext = inNext; }

	int64			GetPageNr() const				{ return mPageNr; }
	void			MoveTo(int64 inPageNr);

	void			SetParent(M6IndexBranchPage* inParent);
	M6IndexBranchPage*
					GetParent() const				{ return mParent; }

	bool			IsLeaf() const					{ return mData->mFlags & eM6IndexPageIsLeaf; }
	
//	bool			IsLocked() const				{ return mLocked; }
//	void			SetLock(bool inLocked)			{ mLocked = inLocked; }
//	
//	bool			IsDirty() const					{ return mDirty; }
//	void			SetDirty(bool inDirty)			{ mDirty = inDirty; }
	
	uint32			GetN() const					{ return mData->mN; }
	uint32			Free() const					{ return kM6IndexPageKeySpace - mKeyOffsets[mData->mN] - mData->mN * sizeof(int64); }
	bool			CanStore(const string& inKey)	{ return mData->mN < kM6MaxEntriesPerPage and Free() >= inKey.length() + 1 + sizeof(int64); }
	
	bool			TooSmall() const				{ return Free() > kM6IndexPageMinKeySpace; }
//	bool			TooSmall() const				{ return mData->mN < kM6MinEntriesPerPage; }
	
	void			SetLink(int64 inLink);
	uint32			GetLink() const					{ return mData->mLink; }
	
	virtual bool	Find(const string& inKey, int64& outValue) = 0;
	virtual bool	Insert(string& ioKey, int64& ioValue) = 0;
	virtual bool	Erase(string& ioKey, int32 inIndex) = 0;

	string			GetKey(uint32 inIndex) const;

	int64			GetValue(uint32 inIndex) const;
	void			SetValue(uint32 inIndex, int64 inValue);

	uint32			GetValue32(uint32 inIndex) const;
	void			SetValue32(uint32 inIndex, uint32 inValue);

	void			GetKeyValue(uint32 inIndex, string& outKey, int64& outValue) const;
	void			InsertKeyValue(const string& inKey, int64 inValue, uint32 inIndex);

	bool			GetNext(uint32& ioPage, uint32& ioKey, M6Tuple& outTuple) const;

#if DEBUG
	void			Validate(const string& inKey);
	void			Dump(int inLevel = 0);
#endif

	virtual bool	Underflow(M6IndexPage& inRight, uint32 inIndex) = 0;

	void			EraseEntry(uint32 inIndex);
	void			ReplaceKey(uint32 inIndex, const string& inKey);

  protected:
					M6IndexPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, int64 inPageNr, 
						M6IndexBranchPage* inParent);

	void			BinarySearch(const string& inKey, int32& outIndex, bool& outMatch) const;
	static void		MoveEntries(M6IndexPage& inFrom, M6IndexPage& inTo,
						uint32 inFromOffset, uint32 inToOffset, uint32 inCount);

	M6IndexImpl&		mIndexImpl;
	M6IndexBranchPage*	mParent;	
	M6IndexPage*		mNext;					
	M6IndexPageData*	mData;
	int64				mPageNr;
	int32				mRefCount;
	bool				mLocked;
	bool				mDirty;
	uint16				mKeyOffsets[kM6MaxEntriesPerPage + 1];

  private:

					M6IndexPage(const M6IndexPage&);
	M6IndexPage&	operator=(const M6IndexPage&);

};

class M6IndexLeafPage : public M6IndexPage
{
  public:
					M6IndexLeafPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData,
						int64 inPageNr, M6IndexBranchPage* inParent);

	virtual bool	Find(const string& inKey, int64& outValue);
	virtual bool	Insert(string& ioKey, int64& ioValue);
	virtual bool	Erase(string& ioKey, int32 inIndex);

	virtual bool	Underflow(M6IndexPage& inRight, uint32 inIndex);
};

class M6IndexBranchPage : public M6IndexPage
{
  public:
					M6IndexBranchPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData,
						int64 inPageNr, M6IndexBranchPage* inParent);

	virtual bool	Find(const string& inKey, int64& outValue);
	virtual bool	Insert(string& ioKey, int64& ioValue);
	virtual bool	Erase(string& ioKey, int32 inIndex);

	virtual bool	Underflow(M6IndexPage& inRight, uint32 inIndex);
	
	bool			UpdateLinkKey(const string& inNewKey, int64 inPageNr);
};

// --------------------------------------------------------------------

inline M6IndexPagePtr::M6IndexPagePtr()
	: mPage(nullptr)
{
}

inline M6IndexPagePtr::M6IndexPagePtr(M6IndexPage* inPage)
	: mPage(inPage)
{
	if (mPage != nullptr)
		mPage->Reference();
}

inline M6IndexPagePtr::M6IndexPagePtr(const M6IndexPagePtr& inPtr)
	: mPage(inPtr.mPage)
{
	if (mPage != nullptr)
		mPage->Reference();
}

inline M6IndexPagePtr::~M6IndexPagePtr()
{
	if (mPage != nullptr)
		mPage->Release();
}

inline M6IndexPagePtr&	M6IndexPagePtr::operator=(const M6IndexPagePtr& inPtr)
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

inline M6IndexPage* M6IndexPagePtr::release()
{
	M6IndexPage* result = mPage;
	mPage = nullptr;
	return result;
}

inline void M6IndexPagePtr::reset(M6IndexPage* inPage)
{
	if (mPage != nullptr)
		mPage->Release();
	mPage = inPage;
}

// --------------------------------------------------------------------

M6IndexPage::M6IndexPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, int64 inPageNr,
		M6IndexBranchPage* inParent)
	: mIndexImpl(inIndexImpl)
	, mPageNr(inPageNr)
	, mRefCount(0)
	, mData(inData)
	, mParent(inParent)
	, mNext(nullptr)
	, mLocked(false)
	, mDirty(false)
{
	assert(mData->mN <= kM6MaxEntriesPerPage);
	
	uint8* key = mData->mKeys;
	for (uint32 i = 0; i <= mData->mN; ++i)
	{
		assert(key <= mData->mKeys + kM6IndexPageSize);
		mKeyOffsets[i] = static_cast<uint16>(key - mData->mKeys);
		key += *key + 1;
	}
}

M6IndexPage::~M6IndexPage()
{
	assert(mRefCount == 0);
	assert(not mDirty);
	delete mData;
}

void M6IndexPage::Flush()
{
	if (mDirty)
	{
		mIndexImpl.GetFile().PWrite(*mData, mPageNr * kM6IndexPageSize);
		mDirty = false;
	}
}

void M6IndexPage::Deallocate()
{
	static const M6IndexPageData kEmpty = { eM6IndexPageIsEmpty };
	*mData = kEmpty;
	mDirty = true;
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
		void* dst = inDst.mData->mKeys + inDst.mKeyOffsets[inDstOffset] +
			inSrc.mKeyOffsets[inSrcOffset + inCount] - inSrc.mKeyOffsets[inSrcOffset];
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

	inSrc.mDirty = true;
	inDst.mDirty = true;
}

void M6IndexPage::SetLink(int64 inLink)
{
	if (inLink > numeric_limits<uint32>::max())
		THROW(("Invalid link value"));
	
	mData->mLink = static_cast<uint32>(inLink);
	mDirty = true;
}

void M6IndexPage::SetParent(M6IndexBranchPage* inParent)
{
	assert(mRefCount == 0 or mParent == nullptr or mParent == inParent);
	mParent = inParent;
}

void M6IndexPage::MoveTo(int64 inPageNr)
{
	if (inPageNr != mPageNr)
	{
		M6IndexPagePtr page(mIndexImpl.Cache(inPageNr, mParent));

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

inline uint32 M6IndexPage::GetValue32(uint32 inIndex) const
{
	assert(inIndex < mData->mN);
	int64 v = swap_bytes(mData->mData[kM6IndexPageDataCount - inIndex - 1]);
	if (v > numeric_limits<uint32>::max())
		THROW(("Invalid value"));
	return static_cast<uint32>(v);
}

inline void M6IndexPage::SetValue32(uint32 inIndex, uint32 inValue)
{
	assert(inIndex < mData->mN);
	mData->mData[kM6IndexPageDataCount - inIndex - 1] = swap_bytes(static_cast<int64>(inValue));
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
		M6IndexPagePtr next(mIndexImpl.Cache(ioPage, mParent));
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

M6IndexLeafPage::M6IndexLeafPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, int64 inPageNr,
		M6IndexBranchPage* inParent)
	: M6IndexPage(inIndexImpl, inData, inPageNr, inParent)
{
	assert(mData->mFlags & eM6IndexPageIsLeaf);
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

bool M6IndexLeafPage::Insert(string& ioKey, int64& ioValue)
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
		M6IndexPagePtr next(mIndexImpl.AllocateLeaf(mParent));
	
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

	assert(mData->mN >= kM6MinEntriesPerPage or mParent == nullptr);
	assert(mData->mN <= kM6MaxEntriesPerPage);

	return result;
}

bool M6IndexLeafPage::Erase(string& ioKey, int32 inIndex)
{
	bool result = false, match = false;
	int32 ix;
	BinarySearch(ioKey, ix, match);
	
	if (match)		// match in a leaf page
	{
		// erase the key at ix
		EraseEntry(ix);
		
		if (mParent != nullptr)
		{
			assert(mData->mN > 0);
			if (ix == 0 and mData->mN > 0)	// need to pass on the new key
			{
				if (not mParent->UpdateLinkKey(GetKey(0), mPageNr))
				{
#pragma message("UpdateLinkKey may fail, fix needed?")
					// how bad is this anyway?

//					THROW(("To be implemented"));
				}
			}
		
			if (TooSmall())
			{							// we're not the root page and we've lost too many entries
				if (inIndex + 1 < static_cast<int32>(mParent->GetN()))
				{
					// try to compensate using our right sibling
					M6IndexPagePtr right(mIndexImpl.Cache(mParent->GetValue32(inIndex + 1), mParent));
					Underflow(*right, inIndex + 1);
				}
				
				if (TooSmall() and inIndex >= 0)
				{
					// if still too small, try with the left sibling
					uint32 leftNr;
					if (inIndex > 0)
						leftNr = mParent->GetValue32(inIndex - 1);
					else
						leftNr = mParent->GetLink();

					M6IndexPagePtr left(mIndexImpl.Cache(leftNr, mParent));
					left->Underflow(*this, inIndex);
				}
				
				result = true;
			}
		}
	}
	
	return result;
}

bool M6IndexLeafPage::Underflow(M6IndexPage& inRight, uint32 inIndex)
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
	
		mParent->EraseEntry(inIndex);
		inRight.Deallocate();
	}
	else		// redistribute the data
	{
		// pKey is the key in mParent at inIndex (and, since this a leaf, the first key in right)
		string pKey = mParent->GetKey(inIndex);
		assert(pKey == right.GetKey(0));
		int32 pKeyLen = static_cast<int32>(pKey.length());
		int32 pFree = mParent->Free();
		
		if (Free() > right.Free())	// move items from right to left
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
			mParent->ReplaceKey(inIndex, right.GetKey(0));
		}
		else
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
			mParent->ReplaceKey(inIndex, right.GetKey(0));
		}
	}
	
	return not (TooSmall() or right.TooSmall());
}

// --------------------------------------------------------------------

M6IndexBranchPage::M6IndexBranchPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, int64 inPageNr,
		M6IndexBranchPage* inParent)
	: M6IndexPage(inIndexImpl, inData, inPageNr, inParent)
{
	assert((mData->mFlags & eM6IndexPageIsLeaf) == 0);
}

bool M6IndexBranchPage::Find(const string& inKey, int64& outValue)
{
	bool match;
	int32 ix;
	
	BinarySearch(inKey, ix, match);

	int64 pageNr;
	
	if (ix < 0)
		pageNr = mData->mLink;
	else
		pageNr = GetValue(ix);
	
	M6IndexPagePtr page(mIndexImpl.Cache(pageNr, this));
	return page->Find(inKey, outValue);
}

/*
	Insert returns a bool indicating the depth increased.
	In that case the ioKey and ioValue are updated to the values
	to be inserted in the calling page (or a new root has to be made).
*/

bool M6IndexBranchPage::Insert(string& ioKey, int64& ioValue)
{
	bool result = false, match;
	int32 ix;

	BinarySearch(ioKey, ix, match);
	
	int64 pageNr;
	
	if (ix < 0)
		pageNr = mData->mLink;
	else
		pageNr = GetValue(ix);

	M6IndexPagePtr page(mIndexImpl.Cache(pageNr, this));
	if (page->Insert(ioKey, ioValue))
	{
		// page was split, we now need to store ioKey in our page
		ix += 1;	// we need to insert at ix + 1

		if (CanStore(ioKey))
			InsertKeyValue(ioKey, ioValue, ix);
		else
		{
			// the key needs to be inserted but it didn't fit
			// so we need to split the page

			M6IndexPagePtr next(mIndexImpl.AllocateBranch(mParent));
			
			int32 split = mData->mN / 2;
			string upKey;
			uint32 downPage;

			if (ix == split)
			{
				upKey = ioKey;
				downPage = static_cast<uint32>(ioValue);

				MoveEntries(*this, *next, split, 0, mData->mN - split);
			}
			else if (ix < split)
			{
				--split;
				upKey = GetKey(split);
				downPage = GetValue32(split);

				MoveEntries(*this, *next, split + 1, 0, mData->mN - split - 1);
				mData->mN -= 1;

				if (ix <= split)
					InsertKeyValue(ioKey, ioValue, ix);
				else
					next->InsertKeyValue(ioKey, ioValue, ix - split - 1);
			}
			else
			{
				upKey = GetKey(split);
				downPage = GetValue32(split);

				MoveEntries(*this, *next, split + 1, 0, mData->mN - split - 1);
				mData->mN -= 1;

				if (ix < split)
					InsertKeyValue(ioKey, ioValue, ix);
				else
					next->InsertKeyValue(ioKey, ioValue, ix - split - 1);
			}

			next->SetLink(downPage);
			
			ioKey = upKey;
			ioValue = next->GetPageNr();
			
			result = true;
		}
	}

	assert(mData->mN >= kM6MinEntriesPerPage or mParent == nullptr);
	assert(mData->mN <= kM6MaxEntriesPerPage);

	return result;
}

bool M6IndexBranchPage::Erase(string& ioKey, int32 inIndex)
{
	bool result = false, match = false;
	int32 ix;
	
	BinarySearch(ioKey, ix, match);

	int64 pageNr;
	
	if (ix < 0)
		pageNr = mData->mLink;
	else
		pageNr = GetValue32(ix);
	
	M6IndexPagePtr page(mIndexImpl.Cache(pageNr, this));
	if (page->Erase(ioKey, ix))
	{
		if (TooSmall() and mParent != nullptr)
		{
			if (inIndex + 1 < static_cast<int32>(mParent->GetN()))
			{
				// try to compensate using our right sibling
				M6IndexPagePtr right(mIndexImpl.Cache(mParent->GetValue32(inIndex + 1), mParent));
				Underflow(*right, inIndex + 1);
			}
			
			if (TooSmall() and inIndex >= 0)
			{
				// if still too small, try with the left sibling
				M6IndexPagePtr left(mIndexImpl.Cache(inIndex > 0 ? mParent->GetValue32(inIndex - 1) : mParent->GetLink(), mParent));
				left->Underflow(*this, inIndex);
			}
		}

		result = true;
	}

	return result;
}

bool M6IndexBranchPage::Underflow(M6IndexPage& inRight, uint32 inIndex)
{
	M6IndexBranchPage& right(static_cast<M6IndexBranchPage&>(inRight));

	// This page left of inRight contains too few entries, see if we can fix this
	// first try a merge

	// pKey is the key in inParent at inIndex (and, since this a leaf, the first key in inRight)
	string pKey = mParent->GetKey(inIndex);
	int32 pKeyLen = static_cast<int32>(pKey.length());

	if (Free() + inRight.Free() - pKeyLen - sizeof(int64) >= kM6IndexPageKeySpace and
		mData->mN + right.mData->mN + 1 <= kM6MaxEntriesPerPage)
	{
		InsertKeyValue(pKey, inRight.GetLink(), mData->mN);
		
		// join the pages
		MoveEntries(right, *this, 0, mData->mN, right.mData->mN);
	
		mParent->EraseEntry(inIndex);
		inRight.Deallocate();
	}
	else		// redistribute the data
	{
		if (Free() > inRight.Free())	// rotate an entry from right to left
		{									// but only if it fits in the parent
			string rKey = inRight.GetKey(0);
			int32 delta = static_cast<int32>(rKey.length() - pKey.length());
			if (delta <= static_cast<int32>(mParent->Free()))
			{
				InsertKeyValue(pKey, right.mData->mLink, mData->mN);
				mParent->ReplaceKey(inIndex, rKey);
				mParent->SetValue32(inIndex, right.mPageNr);
				right.mData->mLink = inRight.GetValue32(0);
				inRight.EraseEntry(0);
			}
		}
		else
		{
			string lKey = GetKey(mData->mN - 1);
			int32 delta = static_cast<int32>(lKey.length() - pKey.length());
			if (delta <= static_cast<int32>(mParent->Free()))
			{
				inRight.InsertKeyValue(pKey, right.mData->mLink, 0);
				right.mData->mLink = GetValue32(mData->mN - 1);
				mParent->ReplaceKey(inIndex, lKey);
				EraseEntry(mData->mN - 1);
			}
		}
	}
	
	return not (TooSmall() or inRight.TooSmall());
}

bool M6IndexBranchPage::UpdateLinkKey(const string& inNewKey, uint32 inPageNr)
{
	bool result = true;
	
	// we are called by page 'inPageNr', if it is our link page, pass it on
	if (inPageNr == mData->mLink)
	{
		if (mParent != nullptr)
			result = mParent->UpdateLinkKey(inNewKey, mPageNr);
	}
	else
	{
		uint32 index = -1;
		
		for (int32 i = 0; i < mData->mN; ++i)
		{
			if (GetValue32(i) == inPageNr)
			{
				index = i;
				break;
			}
		}
		assert(index >= 0);
		
		string oldKey = GetKey(index);
		int32 delta = static_cast<int32>(inNewKey.length() - oldKey.length());
		if (delta < static_cast<int32>(Free()))
			ReplaceKey(index, inNewKey);
		else
			result = false;
	}
	
	return result;
}

// --------------------------------------------------------------------

M6IndexImpl::M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, MOpenMode inMode)
	: mFile(inPath, inMode)
	, mIndex(inIndex)
	, mDirty(false)
	, mAutoCommit(true)
	, mCache(nullptr)
	, mCachedCount(0)
{
	if (inMode == eReadWrite and mFile.Size() == 0)
	{
		M6IxFileHeaderPage page = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
		mFile.PWrite(&page, kM6IndexPageSize, 0);

		// and create a root page to keep code simple
		M6IndexPagePtr root(AllocateLeaf(nullptr));

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
	, mCache(nullptr)
	, mCachedCount(0)
{
	mFile.Truncate(0);
	
	M6IxFileHeaderPage data = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
	mFile.PWrite(&data, kM6IndexPageSize, 0);

	mHeader = data.mHeader;
	mFile.PWrite(mHeader, 0);
	
	try
	{
		// inData is sorted, so we start by writing out leaf pages:
		M6IndexPagePtr page(AllocateLeaf(nullptr));
		
		deque<M6Tuple> up;		// keep track of the nodes we have to create for the next levels
		M6Tuple tuple;
		
		tuple.value = page->GetPageNr();
		up.push_back(tuple);
		
		while (inData(tuple))
		{
			if (not page->CanStore(tuple.key))
			{
				M6IndexPagePtr next(AllocateLeaf(nullptr));
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

	M6IndexPage* page = mCache;
	while (page != nullptr)
	{
		M6IndexPage* next = page->GetNext();
		delete page;
		page = next;
	}
}

M6IndexPagePtr M6IndexImpl::AllocateLeaf(M6IndexBranchPage* inParent)
{
	int64 fileSize = mFile.Size();
	int64 pageNr = (fileSize - 1) / kM6IndexPageSize + 1;
	
	if (pageNr > numeric_limits<uint32>::max())
		THROW(("Index too large"));
	
	int64 offset = pageNr * kM6IndexPageSize;
	mFile.Truncate(offset + kM6IndexPageSize);
	
	M6IndexPageData* data = new M6IndexPageData;
	memset(data, 0, kM6IndexPageSize);
	data->mFlags = eM6IndexPageIsLeaf;
	
	M6IndexPage* page = new M6IndexLeafPage(*this, data, static_cast<uint32>(pageNr), inParent);
	page->SetNext(mCache);
	mCache = page;
	++mCachedCount;
	
	return M6IndexPagePtr(page);
}

M6IndexPagePtr M6IndexImpl::AllocateBranch(M6IndexBranchPage* inParent)
{
	int64 fileSize = mFile.Size();
	int64 pageNr = (fileSize - 1) / kM6IndexPageSize + 1;
	
	if (pageNr > numeric_limits<uint32>::max())
		THROW(("Index too large"));
	
	int64 offset = pageNr * kM6IndexPageSize;
	mFile.Truncate(offset + kM6IndexPageSize);
	
	M6IndexPageData* data = new M6IndexPageData;
	memset(data, 0, kM6IndexPageSize);
	
	M6IndexPage* page = new M6IndexBranchPage(*this, data, static_cast<uint32>(pageNr), inParent);
	page->SetNext(mCache);
	mCache = page;
	++mCachedCount;
	
	return M6IndexPagePtr(page);
}

M6IndexPagePtr M6IndexImpl::Cache(uint32 inPageNr, M6IndexBranchPage* inParent)
{
	if (inPageNr == 0)
		THROW(("Invalid page number"));

	M6IndexPage* page = mCache;

	while (page != nullptr and page->GetPageNr() != inPageNr)
	{
		M6IndexPage* next = page->GetNext();
		if (next != nullptr and next->GetPageNr() == inPageNr)
		{
			page->SetNext(next->GetNext());
			next->SetNext(mCache);
			mCache = next;
		}
		page = next;
	}
		
	if (page != nullptr)
		page->SetParent(inParent);
	else
	{
		M6IndexPageData* data = new M6IndexPageData;
		mFile.PRead(*data, inPageNr * kM6IndexPageSize);

		if (data->mFlags & eM6IndexPageIsLeaf)
			page = new M6IndexLeafPage(*this, data, inPageNr, inParent);
		else
			page = new M6IndexBranchPage(*this, data, inPageNr, inParent);

		page->SetNext(mCache);
		mCache = page;
		++mCachedCount;
	}
	
	// now see if we need to clean up a bit
	if (mCachedCount > 2 * kM6LRUCacheSize)
	{
		deque<M6IndexPage*> candidates;
		
		for (M6IndexPage* p = mCache->GetNext(); p != nullptr; p = p->GetNext())
		{
			// only delete older, unreferenced pages 
			if (p->GetRefCount() == 0)
				candidates.push_back(p);
		}
		
		while (candidates.size() > kM6LRUCacheSize)
			candidates.pop_front();
		
		M6IndexPage* p = mCache;
		while (p != nullptr and not candidates.empty())
		{
			if (p->GetNext() == candidates.front())
			{
				M6IndexPage* p2 = candidates.front();
				candidates.pop_front();
				p->SetNext(p2->GetNext());
				delete p2;

				--mCachedCount;
				continue;
			}
			p = p->GetNext();
		}
	}
	
	return M6IndexPagePtr(page);
}

void M6IndexImpl::Commit()
{
	M6IndexPage* page = mCache;
	M6IndexPage* last = nullptr;
	uint32 n = 0;
	
	while (page != nullptr)
	{
		page->Flush();
		M6IndexPage* next = page->GetNext();
		
		if (++n == kM6LRUCacheSize)
		{
			last = next;
			page->SetNext(nullptr);
		}

		page = next;
	}
	
	page = last;
	while (page != nullptr)
	{
		M6IndexPage* next = page->GetNext();
		delete page;
		page = next;
	}

	assert(n == mCachedCount);
	mCachedCount = n;
	if (mCachedCount > kM6LRUCacheSize)
		mCachedCount = kM6LRUCacheSize;
}

void M6IndexImpl::Rollback()
{
	M6IndexPage* page = mCache;
	while (page != nullptr)
	{
		M6IndexPage* next = page->GetNext();
		delete page;
		page = next;
	}

	mCache = nullptr;
	mCachedCount = 0;
}

void M6IndexImpl::SetAutoCommit(bool inAutoCommit)
{
	mAutoCommit = inAutoCommit;
}

void M6IndexImpl::Insert(const string& inKey, int64 inValue)
{
	try
	{
		M6IndexPagePtr root(Cache(mHeader.mRoot, nullptr));
		
		string key(inKey);
		int64 value(inValue);
	
		if (root->Insert(key, value))
		{
			// increase depth
			++mHeader.mDepth;
	
			M6IndexPagePtr newRoot(AllocateBranch(nullptr));
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

void M6IndexImpl::Erase(const string& inKey)
{
	try
	{
		M6IndexPagePtr root(Cache(mHeader.mRoot, nullptr));
		
		string key(inKey);
	
		if (root->Erase(key, 0))
		{
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
}

bool M6IndexImpl::Find(const string& inKey, int64& outValue)
{
	M6IndexPagePtr root(Cache(mHeader.mRoot, nullptr));
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
			M6IndexPagePtr page(Cache(pageNr, nullptr));
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
			M6IndexPagePtr page(Cache(pageNr, nullptr));
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
				M6IndexPagePtr next(Cache(ix1[link], nullptr));
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
		
		M6IndexPagePtr page(AllocateBranch(nullptr));
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
			page = AllocateBranch(nullptr);
			page->SetLink(tuple.value);

			nextUp.push_back(M6Tuple(tuple.key, page->GetPageNr()));
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
		M6IndexPagePtr page(Cache(pageNr, nullptr));
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
		M6IndexPagePtr page(mIndex->Cache(mPage, nullptr));
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
	M6IndexPagePtr page(mIndex->Cache(mPage, nullptr));
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
						: mPageNr(inPage->GetPageNr()), mReason(inReason)
					{
					}
			
	const char*		what() throw() { return mReason.c_str(); }
		
	uint32			mPageNr;
	string			mReason;
};

#define M6VALID_ASSERT(cond)	do { if (not (cond)) throw M6ValidationException(this, #cond ); } while (false)

void M6IndexPage::Validate(const string& inKey)
{
	if (IsLeaf())
	{
		M6VALID_ASSERT(mData->mN >= kM6MinEntriesPerPage or mParent == nullptr);
		M6VALID_ASSERT(mData->mN <= kM6MaxEntriesPerPage);
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
			M6IndexPagePtr next(mIndexImpl.Cache(mData->mLink, mParent));
			M6VALID_ASSERT(mIndexImpl.CompareKeys(GetKey(mData->mN - 1), next->GetKey(0)) < 0);
		}
	}
	else
	{
		M6VALID_ASSERT(mData->mN >= kM6MinEntriesPerPage or mParent == nullptr);
		M6VALID_ASSERT(mData->mN <= kM6MaxEntriesPerPage);

		for (uint32 i = 0; i < mData->mN; ++i)
		{
			M6IndexPagePtr link(mIndexImpl.Cache(mData->mLink, static_cast<M6IndexBranchPage*>(this)));
			link->Validate(inKey);
			
			for (uint32 i = 0; i < mData->mN; ++i)
			{
				M6IndexPagePtr page(mIndexImpl.Cache(GetValue32(i), static_cast<M6IndexBranchPage*>(this)));
				page->Validate(GetKey(i));
				if (i > 0)
					M6VALID_ASSERT(mIndexImpl.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
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
			M6IndexPagePtr next(mIndexImpl.Cache(mData->mLink, mParent));
			cout << prefix << "  " << "link: " << next->GetKey(0) << endl;
		}
	}
	else
	{
		cout << prefix << (mParent ? "branch" : "root") << " page at " << mPageNr << "; N = " << mData->mN << ": {";
		for (int i = 0; i < mData->mN; ++i)
			cout << GetKey(i) << (i + 1 < mData->mN ? ", " : "");
		cout << "}" << endl;

		M6IndexPagePtr link(mIndexImpl.Cache(mData->mLink, static_cast<M6IndexBranchPage*>(this)));
		link->Dump(inLevel + 1);
		
		for (int i = 0; i < mData->mN; ++i)
		{
			cout << prefix << inLevel << '.' << i << ") " << GetKey(i) << endl;
			
			M6IndexPagePtr sub(mIndexImpl.Cache(GetValue32(i), static_cast<M6IndexBranchPage*>(this)));
			sub->Dump(inLevel + 1);
		}
	}
}

void M6IndexImpl::Validate()
{
	try
	{
		M6IndexPagePtr root(Cache(mHeader.mRoot, nullptr));
		root->Validate("");
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

	M6IndexPagePtr root(Cache(mHeader.mRoot, nullptr));
	root->Dump(0);
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
