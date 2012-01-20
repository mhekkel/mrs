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

class M6IndexImpl
{
  public:
					M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, bool inCreate);
					~M6IndexImpl();

	void			Insert(const string& inKey, int64 inValue);
	bool			Find(const string& inKey, int64& outValue);
	
	uint32			Size() const				{ return mHeader.mSize; }
	
	M6File&			GetFile()					{ return mFile; }
	
	int				CompareKeys(const char* inKeyA, size_t inKeyLengthA,
						const char* inKeyB, size_t inKeyLengthB) const
					{
						return mIndex.CompareKeys(inKeyA, inKeyLengthA, inKeyB, inKeyLengthB);
					}

  private:
	M6File			mFile;
	M6BasicIndex&	mIndex;
	M6IxFileHeader	mHeader;
	bool			mDirty;
};

// --------------------------------------------------------------------

class M6IndexPage
{
  public:
					M6IndexPage(M6IndexImpl& inIndex);
					M6IndexPage(M6IndexImpl& inIndex, uint32 inPageNr);
					M6IndexPage(M6IndexImpl& inIndex,
						uint32 inPreviousRoot, const string& inKey, int64 inValue);
					~M6IndexPage();
	
	void			SetLink(uint32 inLink);
	
	bool			Insert(const string& inKey, int64 inValue,
						string& outKey, int64& outValue);
	bool			Find(const string& inKey, int64& outValue);
	
	uint32			GetPageNr() const								{ return mPageNr; }
	int				CompareKeys(uint32 inA, uint32 inB) const;

	void			GetTuple(uint32 inKey, M6Tuple& outTuple) const;
	bool			GetNext(uint32& ioPage, uint32& ioKey, M6Tuple& outTuple) const;

  private:

	void			Split(string& outFirstKey, int64& outPageNr);
	void			Insert(const string& inKey, int64 inValue, uint32 inIndex);

	M6IndexImpl&	mIndex;
	M6File&			mFile;
	M6IndexPageData	mData;
	uint16			mKeyOffsets[kM6MaxEntriesPerPage + 1];
	uint32			mPageNr;
	bool			mDirty;
};

M6IndexPage::M6IndexPage(M6IndexImpl& inIndex)
	: mIndex(inIndex)
	, mFile(mIndex.GetFile())
	, mDirty(true)
{
	int64 fileSize = mFile.Size();
	mPageNr = static_cast<uint32>((fileSize - 1) / kM6IndexPageSize) + 1;
	int64 offset = mPageNr * kM6IndexPageSize;
	mFile.Truncate(offset + kM6IndexPageSize);
	
	// clear the data
	static const M6IndexPageData data = { eM6IndexPageIsLeaf };
	mData = data;
}

M6IndexPage::M6IndexPage(M6IndexImpl& inIndex,
		uint32 inPreviousRoot, const string& inKey, int64 inValue)
	: mIndex(inIndex)
	, mFile(mIndex.GetFile())
	, mDirty(true)
{
	int64 fileSize = mFile.Size();
	mPageNr = static_cast<uint32>((fileSize - 1) / kM6IndexPageSize) + 1;
	int64 offset = mPageNr * kM6IndexPageSize;
	mFile.Truncate(offset + kM6IndexPageSize);
	
	// init the data
	static const M6IndexPageData data = {};
	mData = data;
	mData.mLink = inPreviousRoot;
	mData.mN = 1;
	mData.mData[kM6IndexPageDataCount - 1] = inValue;
	mData.mKeys[0] = static_cast<uint8>(inKey.length());
	memcpy(mData.mKeys + 1, inKey.c_str(), inKey.length());
}

M6IndexPage::M6IndexPage(M6IndexImpl& inIndex, uint32 inPageNr)
	: mIndex(inIndex)
	, mFile(mIndex.GetFile())
	, mPageNr(inPageNr)
	, mDirty(false)
{
	int64 offset = mPageNr * kM6IndexPageSize;
	
	mFile.PRead(&mData, sizeof(mData), offset);
	
	assert(mData.mN <= kM6MaxEntriesPerPage);
	
	uint8* key = mData.mKeys;
	for (uint32 i = 0; i <= mData.mN; ++i)
	{
		mKeyOffsets[i] = static_cast<uint16>(key - mData.mKeys);
		key += *key + 1;
		assert(key <= mData.mKeys + kM6IndexPageSize);
	}
}

M6IndexPage::~M6IndexPage()
{
	if (mDirty)
		mFile.PWrite(&mData, sizeof(mData), mPageNr * kM6IndexPageSize);
}

void M6IndexPage::SetLink(uint32 inLink)
{
	mData.mLink = inLink;
	mData.mFlags &= ~(eM6IndexPageIsLeaf);
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

void M6IndexPage::GetTuple(uint32 inKey, M6Tuple& outTuple) const
{
	assert(inKey < mData.mN);

	const uint8* key = mData.mKeys + mKeyOffsets[inKey];
	outTuple.key.assign(reinterpret_cast<const char*>(key) + 1, *key);
	outTuple.value = mData.mData[kM6IndexPageDataCount - inKey - 1];
}

bool M6IndexPage::GetNext(uint32& ioPage, uint32& ioKey, M6Tuple& outTuple) const
{
	bool result = false;
	++ioKey;
	if (ioKey < mData.mN)
		result = true;
	else if (mData.mLink != 0)
	{
		M6IndexPage next(mIndex, mData.mLink);
		ioKey = 0;
		next.GetTuple(ioKey, outTuple);
	}
	
	if (result)
		GetTuple(ioKey, outTuple);
	
	return result;
}

/*
	Insert returns a bool indicating the depth increased.
	In that case the ioKey and ioValue are updated to the values
	to be inserted in the page a level up.
*/

bool M6IndexPage::Insert(const string& inKey, int64 inValue, string& outKey, int64& outValue)
{
	bool result = false;

	// Start by locating a position in the page
	int L = 0, R = mData.mN - 1;
	while (L <= R)
	{
		int i = (L + R) / 2;

		const uint8* ko = mData.mKeys + mKeyOffsets[i];
		const char* k = reinterpret_cast<const char*>(ko + 1);

		int d = mIndex.CompareKeys(inKey.c_str(), inKey.length(), k, *ko);
		if (d < 0)
			R = i - 1;
		else
			L = i + 1;
	}

	// will need this
	uint32 free = kM6IndexPageKeySpace - mKeyOffsets[mData.mN] - mData.mN * sizeof(int64);
	
	// R now points to the first key greater or equal to inKey
	// or it is -1 one if all keys are larger than inKey

	if (mData.mFlags & eM6IndexPageIsLeaf)
	{
		// leaf node. First calculate whether the tuple will fit in this page

		// Store the key at R + 1
		uint32 ix = R + 1;

		// if not, we have to split this page.
		if (mData.mN >= kM6MaxEntriesPerPage or
			free < inKey.length() + 1 + sizeof(inValue))
		{
			Split(outKey, outValue);
			result = true;

			mData.mLink = static_cast<uint32>(outValue);
			
			if (ix > mData.mN)
			{
				M6IndexPage next(mIndex, mData.mLink);
				
				string k;
				int64 v;

				bool r = next.Insert(inKey, inValue, k, v);
				assert(r == false);
			}
			
			free = kM6IndexPageKeySpace - mKeyOffsets[mData.mN] + mData.mN * sizeof(int64);
		}
		
		assert(free >= inKey.length() + 1 + sizeof(inValue));
		
		if (ix <= mData.mN)		// it fits. Store it.
			Insert(inKey, inValue, ix);
	}
	else
	{
		// if this is a non-leaf page, propagate the Insert
		uint32 pageNr;
		
		if (R < 0)
			pageNr = mData.mLink;
		else
			pageNr = static_cast<uint32>(mData.mData[kM6IndexPageDataCount - R - 1]);
		
		M6IndexPage page(mIndex, pageNr);
		if (page.Insert(inKey, inValue, outKey, outValue))
		{
			// Store the key at R + 1
			uint32 ix = R + 1;

			// need to insert key since a new page was added
			string k = outKey;
			int64 v = outValue;
			
			if (free < k.length() + 1 + sizeof(v))
			{
				Split(outKey, outValue);
				result = true;
			}
			
			Insert(k, v, ix);
		}
	}

	return result;
}

void M6IndexPage::Split(string& outFirstKey, int64& outPageNr)
{
	M6IndexPage next(mIndex);

	M6IndexPageData& ld = mData;
	M6IndexPageData& rd = next.mData;
	
	uint32 N = mData.mN;
	rd.mN = ld.mN / 2;
	ld.mN -= rd.mN;
	
	// copy keys
	void* src = ld.mKeys + mKeyOffsets[ld.mN];
	void* dst = rd.mKeys;
	uint32 n = mKeyOffsets[N] - mKeyOffsets[ld.mN];
	memcpy(dst, src, n);
	
	// copy data
	src = ld.mData + kM6IndexPageDataCount - N;
	dst = rd.mData + kM6IndexPageDataCount - rd.mN;
	n = rd.mN * sizeof(int64);
	memcpy(dst, src, n);
	
	// update rest
	rd.mFlags = ld.mFlags;
	rd.mLink = ld.mLink;
	ld.mLink = next.GetPageNr();
	
	// including the key offsets
	uint8* key = rd.mKeys;
	for (uint32 i = 0; i <= rd.mN; ++i)
	{
		next.mKeyOffsets[i] = static_cast<uint16>(key - rd.mKeys);
		key += *key + 1;
		assert(key <= rd.mKeys + kM6IndexPageSize);
	}
	
	outFirstKey.assign(reinterpret_cast<char*>(rd.mKeys + 1), *rd.mKeys);
	outPageNr = next.GetPageNr();

	mDirty = true;
}

void M6IndexPage::Insert(const string& inKey, int64 inValue, uint32 inIndex)
{
	if (inIndex < mData.mN)
	{
		uint8* src = mData.mKeys + mKeyOffsets[inIndex];
		uint8* dst = src + inKey.length() + 1;
		
		// shift keys
		memmove(dst, src, mKeyOffsets[mData.mN] - mKeyOffsets[inIndex]);
		
		// shift data
		int64* dsrc = mData.mData + kM6IndexPageDataCount - mData.mN;
		int64* ddst = dsrc - 1;
		
		memmove(ddst, dsrc, (mData.mN - inIndex) * sizeof(int64));
		
		// shift key offsets
		for (uint32 i = inIndex + 1; i <= mData.mN; ++i)
			mKeyOffsets[i] = static_cast<uint16>(mKeyOffsets[i - 1] + inKey.length() + 1);
	}
	
	uint8* k = mData.mKeys + mKeyOffsets[inIndex];
	*k = static_cast<uint8>(inKey.length());
	memcpy(k + 1, inKey.c_str(), *k);
	mData.mData[kM6IndexPageDataCount - inIndex - 1] = inValue;
	++mData.mN;

	mDirty = true;
}

bool M6IndexPage::Find(const string& inKey, int64& outValue)
{
	bool result = false;

	// Start by locating a position in the page
	int L = 0, R = mData.mN - 1;
	while (L <= R)
	{
		int i = (L + R) / 2;

		const uint8* ko = mData.mKeys + mKeyOffsets[i];
		const char* k = reinterpret_cast<const char*>(ko + 1);

		int d = mIndex.CompareKeys(inKey.c_str(), inKey.length(), k, *ko);
		
		// found ?
		if (d == 0 and mData.mFlags & eM6IndexPageIsLeaf)
		{
			result = true;
			outValue = mData.mData[kM6IndexPageDataCount - i - 1];
			break;
		}
		else if (d < 0)
			R = i - 1;
		else
			L = i + 1;
	}
	
	if (not (mData.mFlags & eM6IndexPageIsLeaf))
	{
		uint32 pageNr;
		
		if (R < 0)
			pageNr = mData.mLink;
		else
			pageNr = static_cast<uint32>(mData.mData[kM6IndexPageDataCount - R - 1]);
		
		M6IndexPage page(mIndex, pageNr);
		result = page.Find(inKey, outValue);
	}
	
	return result;
}

// --------------------------------------------------------------------

M6IndexImpl::M6IndexImpl(M6BasicIndex& inIndex, const string& inPath, bool inCreate)
	: mFile(inPath, inCreate ? eReadWrite : eReadOnly)
	, mIndex(inIndex)
	, mDirty(false)
{
	if (inCreate)
	{
		M6IxFileHeaderPage page = { kM6IndexFileSignature, sizeof(M6IxFileHeader) };
		mFile.PWrite(&page, kM6IndexPageSize, 0);

		// and create a root page to keep code simple
		M6IndexPage root(*this);
		page.mHeader.mRoot = root.GetPageNr();

		mHeader = page.mHeader;
		mDirty = true;
	}
	else
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
	M6IndexPage root(*this, mHeader.mRoot);
	
	string key;
	int64 value;

	if (root.Insert(inKey, inValue, key, value))
	{
		// increase depth
		++mHeader.mDepth;
		
		// construct a new root page (using special constructor)
		M6IndexPage newRoot(*this, mHeader.mRoot, key, value);
		mHeader.mRoot = newRoot.GetPageNr();
	}

	++mHeader.mSize;
	mDirty = true;
}

bool M6IndexImpl::Find(const string& inKey, int64& outValue)
{
	M6IndexPage root(*this, mHeader.mRoot);
	return root.Find(inKey, outValue);
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
	M6IndexPage page(*mIndex, mPage);
	page.GetTuple(mKeyNr, mCurrent);
}

M6BasicIndex::iterator& M6BasicIndex::iterator::operator=(const iterator& iter)
{
	if (this != &iter)
	{
		mIndex = iter.mIndex;
		mPage = iter.mPage;
		mKeyNr = iter.mKeyNr;
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
	}

	return *this;
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

bool M6BasicIndex::Find(const string& inKey, int64& outValue)
{
	return mImpl->Find(inKey, outValue);
}

uint32 M6BasicIndex::Size() const
{
	return mImpl->Size();
}

