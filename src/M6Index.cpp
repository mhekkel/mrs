#include "M6Lib.h"
#include "M6File.h"

#include <boost/static_assert.hpp>
#include <boost/tr1/tuple.hpp>
#include <vector>

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
	kM6IndexPageSize = 512,
	kM6IndexPageHeaderSize = 8,
	kM6MaxEntriesPerPage = (kM6IndexPageSize - kM6IndexPageHeaderSize) / 12,	// keeps code simple
	kM6IndexPageKeySpace = kM6IndexPageSize - kM6IndexPageHeaderSize,
	kM6IndexPageDataCount = (kM6IndexPageKeySpace / sizeof(int64));

BOOST_STATIC_ASSERT(kM6IndexPageDataCount >= kM6MaxEntriesPerPage);

enum {
	eM6IndexPageIsLeaf		= (1 << 0),
	eM6IndexPageBigEndian	= (1 << 1),
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
	uint32		mFirstLeaf;
	uint32		mLastLeaf;
	uint32		mDepth;
};

union M6IxFileHeaderPage
{
	M6IxFileHeader	mHeader;
	uint8			mFiller[kM6IndexPageSize];
};

BOOST_STATIC_ASSERT(sizeof(M6IxFileHeaderPage) == kM6IndexPageSize);

// --------------------------------------------------------------------

class M6IndexPage
{
  public:
					M6IndexPage(M6File& inFile, M6BasicIndex& inIndex, uint32 inPageNr, bool inClear);
					~M6IndexPage();
	
	bool			Insert(string& ioKey, int64& ioValue);
	uint32			GetPageNr() const								{ return mPageNr; }
	int				CompareKeys(uint32 inA, uint32 inB) const;

  private:
	M6File&			mFile;
	M6BasicIndex&	mIndex;
	M6IndexPageData	mData;
	uint16			mKeyOffsets[kM6MaxEntriesPerPage + 1];
	int64*			mE;
	uint32			mPageNr;
	bool			mDirty;
};

M6IndexPage::M6IndexPage(M6File& inFile, M6BasicIndex& inIndex, uint32 inPageNr, bool inClear)
	: mFile(inFile)
	, mIndex(inIndex)
	, mE(mData.mData + kM6IndexPageDataCount - 1)
	, mPageNr(inPageNr)
	, mDirty(inClear)
{
	int64 offset = mPageNr * kM6IndexPageSize;
	
	if (inClear)
	{
		if (mFile.Size() < offset + kM6IndexPageSize)
			mFile.Truncate(offset + kM6IndexPageSize);
		
		// clear the data
		memset(&mData, sizeof(mData), 0);
		memset(mKeyOffsets, sizeof(mKeyOffsets), 0);
	}
	else
	{
		mFile.PRead(&mData, sizeof(mData), offset);
		
		assert(mData.mN <= kM6MaxEntriesPerPage);
		
		uint8* key = mData.mKeys;
		for (uint32 i = 0; i < mData.mN; ++i)
		{
			mKeyOffsets[i] = static_cast<uint16>(key - mData.mKeys);
			key += *key + 1;
			assert(key <= mData.mKeys + kM6IndexPageSize);
		}
	}
}

M6IndexPage::~M6IndexPage()
{
	if (mDirty)
		mFile.PWrite(&mData, sizeof(mData), mPageNr * kM6IndexPageSize);
}

int M6IndexPage::CompareKeys(uint32 inA, uint32 inB) const
{
	assert(inA < mData.mN);
	assert(inB < mData.mN);
	
	const uint8* ka = mData.mKeys + mKeyOffsets[inA];
	const uint8* kb = mData.mKeys + mKeyOffsets[inB];
	
	const char* a = reinterpret_cast<const char*>(ka + 1);
	const char* b = reinterpret_cast<const char*>(kb + 1);
	
	return mIndex.CompareKeys(a, *ka, b, *kb);
}

/*
	Insert returns a bool indicating the depth increased.
	In that case the ioKey and ioValue are updated to the values
	to be inserted in the page a level up.
*/

bool M6IndexPage::Insert(string& ioKey, int64& ioValue)
{
	bool result = false;

	// Start by locating a position in the page
	int L = 0, R = mData.mN - 1;
	while (L <= R)
	{
		int i = (L + R) / 2;

		const uint8* ko = mData.mKeys + mKeyOffsets[i];
		const char* k = reinterpret_cast<const char*>(ko + 1);

		int d = mIndex.CompareKeys(ioKey.c_str(), ioKey.length(), k, *ko);
		if (d < 0)
			R = i - 1;
		else
			L = i + 1;
	}
	
	// R now points to the first key greater or equal to ioKey
	// or it is -1 one if all keys are larger than ioKey

	// if this is a non-leaf page, propagate the Insert
	if (not (mData.mFlags & eM6IndexPageIsLeaf))
	{
		uint32 pageNr;
		
		if (R < 0)
			pageNr = mData.mLink;
		else
			pageNr = static_cast<uint32>(mData.mData[kM6IndexPageDataCount - R - 1]);
		
		M6IndexPage page(mFile, mIndex, pageNr, false);
		if (not page.Insert(ioKey, ioValue))
		{
			// need to increase height
			THROW(("to be implemented"));
		}
	}
	else
	{
		// leaf node. First calculate whether the tuple will fit in this page

		uint32 free = kM6IndexPageKeySpace;
		if (mData.mN > 0)
			free -= mKeyOffsets[mData.mN] + mData.mN * sizeof(int64);

		if (mData.mN < kM6MaxEntriesPerPage and
			free >= ioKey.length() + 1 + sizeof(ioValue))
		{
			// it fits. Store it.
			if (R < mData.mN)
			{
				// shift keys
				memmove(mData.mKeys + mKeyOffsets[R],
					mData.mKeys + mKeyOffsets[R] + ioKey.length() + 1,
					mKeyOffsets[mData.mN] - mKeyOffsets[R]);
				
				// shift data
				memmove(mData.mData + kM6IndexPageDataCount - mData.mN,
					mData.mData + kM6IndexPageDataCount - mData.mN - 1,
					(mData.mN - R) * sizeof(int64));
				
				// shift key offsets
				for (uint32 i = mData.mN + 1; i > R; --i)
					mKeyOffsets[i] = mKeyOffsets[i - 1] + ioKey.length() + 1;
			}
			else
				mKeyOffsets[mData.mN + 1] = mKeyOffsets[mData.mN] + ioKey.length() + 1;
			
			uint8* k = mData.mKeys + mKeyOffsets[R];
			*k = static_cast<uint8>(ioKey.length());
			memcpy(k + 1, ioKey.c_str(), *k);
			
			mData.mData[kM6IndexPageDataCount - R - 1] = ioValue;
			
			++mData.mN;
		}
		else
			THROW(("Unimplemented"));
	}
}

// --------------------------------------------------------------------

class M6IndexImpl
{
  public:
					M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, bool inCreate);
					~M6IndexImpl();

	void			Insert(const string& inKey, int64 inValue);
	
	uint32			Size() const				{ return mHeader.mSize; }

  private:
	M6File			mFile;
	M6BasicIndex&	mIndex;
	M6IxFileHeader	mHeader;
	bool			mDirty;
};

M6IndexImpl::M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, bool inCreate)
	: mFile(inPath, inCreate ? eReadWrite : eReadOnly)
	, mIndex(inIndex)
	, mDirty(false)
{
	if (inCreate)
	{
		M6IxFileHeaderPage page = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
		page.mHeader.mRoot = 1;
		mFile.PWrite(&page, kM6IndexPageSize, 0);

		// and create a root page to keep code simple
		M6IndexPage root(mFile, mIndex, 1, true);
		mDirty = true;
	}

	mFile.PRead(&mHeader, sizeof(mHeader), 0);
	
	assert(mHeader.mSignature == kM6IndexFileSignature);
	assert(mHeader.mHeaderSize == sizeof(M6IxFileHeader));
}

M6IndexImpl::~M6IndexImpl()
{
	if (mDirty)
		mFile.PWrite(&mHeader, sizeof(mHeader), 0);
}

void M6IndexImpl::Insert(const string& inKey, int64 inValue)
{
	M6IndexPage root(mFile, mIndex, mHeader.mRoot, false);
	
	string key(inKey);
	int64 value(inValue);
	if (root.Insert(key, value))
	{
		THROW(("Need to increase height here"));
	}

	++mHeader.mSize;
	mDirty = true;
}

// --------------------------------------------------------------------

M6BasicIndex::iterator::iterator()
{
}

// --------------------------------------------------------------------

M6BasicIndex::M6BasicIndex(const string& inPath, bool inCreate)
	: mImpl(new M6IndexImpl(*this, inPath, inCreate))
{
}

M6BasicIndex::~M6BasicIndex()
{
	delete mImpl;
}

void M6BasicIndex::Insert(const string& key, int64 value)
{
	mImpl->Insert(key, value);
}

uint32 M6BasicIndex::Size() const
{
	return mImpl->Size();
}

