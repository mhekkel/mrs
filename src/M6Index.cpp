#include "M6Lib.h"
#include "M6File.h"

#include <boost/static_assert.hpp>
#include <boost/tr1/tuple.hpp>
#include <vector>

#include "M6Index.h"

// --------------------------------------------------------------------

using namespace std;
using namespace std::tr1;

// --------------------------------------------------------------------

const uint32
	kM6IndexPageSize = 512,
	kM6IndexPageKeySpace = kM6IndexPageSize - sizeof(int64),
	kM6IndexPageDataCount = (kM6IndexPageKeySpace / sizeof(int64));

enum {
	eM6IndexPageIsLeaf		= (1 << 0),
	eM6IndexPageBigEndian	= (1 << 1),
};

struct M6IndexPageData
{
	uint16		mFlags;
	uint16		mN;
	uint32		mFiller;
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
					M6IndexPage(M6File& inFile, uint32 inPageNr, bool inClear);
					~M6IndexPage();
	
	bool			Insert(const string& inKey, int64 inValue);

	uint32			GetPageNr() const				{ return mPageNr; }
	
//	tuple<const char*,const char*>
//					GetKey(uint32 inIndex) const;

	int				CompareKeys(uint32 inA, uint32 inB) const;

  private:
	M6File&			mFile;
	M6IndexPageData	mData;
	vector<uint32>	mKeyOffsets;
	int64*			mE;
	uint32			mPageNr;
	bool			mDirty;
};

M6IndexPage::M6IndexPage(M6File& inFile, uint32 inPageNr, bool inClear)
	: mFile(inFile)
	, mE(mData.mData + kM6IndexPageDataCount - 1)
	, mPageNr(inPageNr)
	, mDirty(inClear)
{
	int64 offset = mPageNr * sizeof(kM6IndexPageSize);
	
	if (inClear)
	{
		if (mFile.Size() < offset + kM6IndexPageSize)
			mFile.Truncate(offset + kM6IndexPageSize);
		
		// clear the data
		memset(&mData, sizeof(mData), 0);
	}
	else
	{
		mFile.PRead(&mData, sizeof(mData), offset);
		
		mKeyOffsets.reserve(mData.mN);
		
		uint8* key = mData.mKeys;
		for (uint32 i = 0; i < mData.mN; ++i)
		{
			mKeyOffsets.push_back(static_cast<uint32>(key - mData.mKeys));
			key += *key + 1;
			assert(key <= mData.mKeys + kM6IndexPageSize);
		}
	}

	assert(mData.mN == mKeyOffsets.size());
}

M6IndexPage::~M6IndexPage()
{
	if (mDirty)
		mFile.PWrite(&mData, sizeof(mData), mPageNr * sizeof(kM6IndexPageSize));
}

int M6IndexPage::CompareKeys(uint32 inA, uint32 inB) const
{
	assert(inA < mData.mN);
	assert(inB < mData.mN);
	
	const uint8* ka = mData.mKeys + mKeyOffsets[inA];
	const uint8* kb = mData.mKeys + mKeyOffsets[inB];
	
	const char* a = reinterpret_cast<const char*>(ka + 1);
	const char* b = reinterpret_cast<const char*>(kb + 1);
	
	int32 n = *ka;
	if (n > *kb)
		n = *kb;
	
	int d = strncmp(a, b, n);
	if (d == 0)
		d = int(*ka) - int(*kb);
	
	return d;
}

bool M6IndexPage::Insert(const string& inKey, int64 inValue)
{
	return false;
}

// --------------------------------------------------------------------

class M6IndexImpl
{
  public:
					M6IndexImpl(const string& inPath, bool inCreate);
					~M6IndexImpl();

	bool			Insert(const string& inKey, int64 inValue);
	
	uint32			Size() const				{ return mHeader.mSize; }

  private:
	M6File			mFile;
	M6IxFileHeader	mHeader;
	bool			mDirty;
};

M6IndexImpl::M6IndexImpl(const string& inPath, bool inCreate)
	: mFile(inPath, inCreate ? eReadWrite : eReadOnly)
	, mDirty(false)
{
	if (inCreate)
	{
		M6IxFileHeaderPage page = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
		mFile.PWrite(&page, kM6IndexPageSize, 0);
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

bool M6IndexImpl::Insert(const string& inKey, int64 inValue)
{
	++mHeader.mSize;
	mDirty = true;
	
	return false;
}

// --------------------------------------------------------------------

M6IndexBase::iterator::iterator()
{
}

// --------------------------------------------------------------------

M6IndexBase::M6IndexBase(const string& inPath, bool inCreate)
	: mImpl(new M6IndexImpl(inPath, inCreate))
{
}

M6IndexBase::~M6IndexBase()
{
	delete mImpl;
}

M6IndexBase::iterator M6IndexBase::Insert(const string& key, int64 value)
{
	mImpl->Insert(key, value);
	return M6IndexBase::iterator();
}

uint32 M6IndexBase::Size() const
{
	return mImpl->Size();
}

int M6IndexBase::CompareKeys(const string& inKeyA, const string& inKeyB) const
{
	return 0;
}
