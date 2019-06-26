//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

// Code to store a B+ index with variable key length and various values.
//
//    TODO: - implement a way to recover deallocated pages
//
//
//    This B+ tree implementation has tree variants. They differ in the
//    data stored in the leaf pages, the branch pages are almost identical.
//    The handling of the keys and data in a page is delegated to a separate
//    class called M6DataAccess.

#include "M6Lib.h"

#include <cmath>
#include <deque>
#include <list>
#include <vector>
#include <numeric>
#include <iostream>
#include <queue>
#include <functional>
#include <tuple>

#include <boost/static_assert.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/thread.hpp>

#include "M6Index.h"
#include "M6Error.h"
#include "M6BitStream.h"
#include "M6Progress.h"
#include "M6Iterator.h"
#include "M6Lexicon.h"
#include "M6Tokenizer.h"

using namespace std;
namespace fs = boost::filesystem;

// DEBUG code

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

    const char*        what() const throw() { return mReason; }

    uint32            mPageNr;
    char            mReason[512];
};

#define M6VALID_ASSERT(cond)    do { if (not (cond)) throw M6ValidationException(this->GetPageNr(), #cond ); } while (false)

// --------------------------------------------------------------------

M6DuplicateKeyException::M6DuplicateKeyException(const string& inKey)
{
    snprintf(mMessage, sizeof(mMessage), "Duplicate key: %s", inKey.c_str());
}

// --------------------------------------------------------------------

int M6NumericComparator::operator()(const char* inKeyA, size_t inKeyLengthA, const char* inKeyB, size_t inKeyLengthB) const
{
    try
    {
        uint32 a = boost::lexical_cast<uint32>(std::string(inKeyA, inKeyLengthA)),
               b = boost::lexical_cast<uint32>(std::string(inKeyB, inKeyLengthB));

        return a - b;
    }
    catch (const boost::bad_lexical_cast &ex)
    {
        return memcmp(inKeyA, inKeyB, std::min(inKeyLengthA, inKeyLengthB));
    }
}

// --------------------------------------------------------------------

int M6FloatComparator::operator()(const char* inKeyA, size_t inKeyLengthA, const char* inKeyB, size_t inKeyLengthB) const
{
    try
    {
        double a = boost::lexical_cast<double>(std::string(inKeyA, inKeyLengthA)),
               b = boost::lexical_cast<double>(std::string(inKeyB, inKeyLengthB));

        if (a > b)
            return 1;

        else if (a < b)
            return -1;
        else
            return 0;
    }
    catch (const boost::bad_lexical_cast &ex)
    {
        return memcmp(inKeyA, inKeyB, std::min(inKeyLengthA, inKeyLengthB));
    }
}

// --------------------------------------------------------------------

// The index will probably never have keys less than 3 bytes in length.
// Including the length byte, this means a minimal key length of 4. Add
// a data element of uint32 and the minimal storage per entry is 8 bytes.
// Given this, the maximum number of keys we will ever store in a page
// is (pageSize - headerSize) / 8. For a 8192 byte page and 8 byte header
// this boils down to 1023.

enum M6IndexPageKind : uint8
{
    eM6IndexEmptyPage            = 'e',
    eM6IndexBranchPage            = 'b',
    eM6IndexSimpleLeafPage        = 'l',
    eM6IndexMultiLeafPage        = 'm',
    eM6IndexMultiIDLLeafPage    = 'i',
    eM6IndexBitVectorPage        = 'v'
};

struct M6IndexPageHeader
{
    M6IndexPageKind    mType;
    uint8            mFlags;
    uint16            mN;
    uint32            mLink;
};

#if 0 //DEBUG

const int64
//    kM6IndexPageSize        = 8192,
    kM6IndexPageSize        = 512,
    kM6IndexPageHeaderSize    = sizeof(M6IndexPageHeader),
    kM6KeySpace                = kM6IndexPageSize - kM6IndexPageHeaderSize,
    kM6MinKeySpace            = kM6KeySpace / 2,
    kM6MaxEntriesPerPage    = 4;
//    kM6MaxEntriesPerPage    = kM6KeySpace / 8;    // see above

const uint32
    kM6MaxKeyLength            = (kM6MinKeySpace / 2 > 255 ? 255 : kM6MinKeySpace / 2),
    kM6BatchSize            = 1024 * 1024;

#else

const int64
    kM6IndexPageSize        = 8192,
    kM6IndexPageHeaderSize    = sizeof(M6IndexPageHeader),
    kM6KeySpace                = kM6IndexPageSize - kM6IndexPageHeaderSize,
    kM6MinKeySpace            = kM6KeySpace / 2,
    kM6MaxEntriesPerPage    = kM6KeySpace / 8;    // see above

const uint32
    kM6MaxKeyLength            = (kM6MinKeySpace / 2 > 255 ? 255 : kM6MinKeySpace / 2),
//    kM6BatchSize            = 1024 * 1024;        // 40 MB at most (MultiIDL)
    kM6BatchSize            =   64 * 1024;        // 2.5 MB at most

#endif

template<M6IndexPageKind>
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

// --------------------------------------------------------------------
//    BitVector is a type used to store small compressed arrays inline
//    and to contain pointers to as well as the leading bytes of larger
//    arrays.

const uint32
    kM6BitVectorSize                = 20,
    kM6BitVectorInlineBitsSize        = kM6BitVectorSize - 1,
    kM6BitVectorLeadingBitsSize        = kM6BitVectorSize - 8;

enum M6BitVectorType : uint8
{
    eM6InlineBitVector,
    eM6OnDiskBitVector
};

union M6BitVector
{
    struct
    {
        M6BitVectorType    mType;
        uint8            mFiller;
        uint16            mOffset;
        uint32            mPage;
        uint8            mLeadingBits[kM6BitVectorLeadingBitsSize];
    };
    struct
    {
        uint8            mType2;
        uint8            mBits[kM6BitVectorInlineBitsSize];
    };
};

BOOST_STATIC_ASSERT(sizeof(M6BitVector) == kM6BitVectorSize);

struct M6MultiData
{
    uint32            mCount;
    M6BitVector        mBitVector;
};

ostream& operator<<(ostream& os, const M6MultiData& d)
{
    os << '<' << d.mCount << '>';
    return os;
}

template<>
struct M6IndexPageDataTraits<eM6IndexMultiLeafPage>
{
    typedef M6MultiData M6DataElement;
};

struct M6MultiIDLData
{
    uint32            mCount;
    M6BitVector        mBitVector;
    int64            mIDLOffset;
};

ostream& operator<<(ostream& os, const M6MultiIDLData& d)
{
    os << '<' << d.mCount << ',' << d.mIDLOffset << '>';
    return os;
}

template<typename T>    uint32 M6CountData(const T& inData);
template<>                uint32 M6CountData(const uint32& inData)            { return 1; }
template<>                uint32 M6CountData(const M6MultiData& inData)        { return inData.mCount; }
template<>                uint32 M6CountData(const M6MultiIDLData& inData)    { return inData.mCount; }

template<>
struct M6IndexPageDataTraits<eM6IndexMultiIDLLeafPage>
{
    typedef M6MultiIDLData M6DataElement;
};

// --------------------------------------------------------------------
//    Each page on disk has the following layout. It starts with a header
//    which is the same for each, and then a combined keys/data area.

template<M6IndexPageKind T>
struct M6IndexPageDataT : public M6IndexPageHeader
{
    typedef typename M6IndexPageDataTraits<T>::M6DataElement M6DataType;

    enum {
        kM6DataCount    = kM6KeySpace / sizeof(M6DataType),
        kM6EntryCount    = (kM6DataCount < kM6MaxEntriesPerPage ? kM6DataCount : kM6MaxEntriesPerPage)
    };

    static const M6IndexPageKind kIndexPageType = T;

    union
    {
        uint8        mKeys[kM6KeySpace];
        M6DataType    mData[kM6DataCount];
    };
};

template<>
struct M6IndexPageDataT<eM6IndexBitVectorPage> : public M6IndexPageHeader
{
    static const M6IndexPageKind kIndexPageType = eM6IndexBitVectorPage;
    uint8    mBits[kM6KeySpace];
};

typedef M6IndexPageDataT<eM6IndexBranchPage>        M6IndexBranchPageData;
typedef M6IndexPageDataT<eM6IndexSimpleLeafPage>    M6IndexSimpleLeafPageData;
typedef M6IndexPageDataT<eM6IndexMultiLeafPage>        M6IndexMultiLeafPageData;
typedef M6IndexPageDataT<eM6IndexMultiIDLLeafPage>    M6IndexMultiIDLLeafPageData;
typedef M6IndexPageDataT<eM6IndexBitVectorPage>        M6IndexBitVectorPageData;

union M6IndexPageData
{
    M6IndexBranchPageData        branch;
    M6IndexSimpleLeafPageData    leaf;
    M6IndexMultiLeafPageData    multi_leaf;
    M6IndexMultiIDLLeafPageData    idl_leaf;
    M6IndexBitVectorPageData    bit_vector;
};

BOOST_STATIC_ASSERT(sizeof(M6IndexPageData) == kM6IndexPageSize);

// --------------------------------------------------------------------
// Glob-like pattern matching

enum M6MatchResult
{
    eM6Match,
    eM6NoMatchAndGreater,
    eM6NoMatchAndEqual,
    eM6NoMatchAndLess
};

static M6MatchResult M6Match(const char* inPattern, const char* inName)
{
    for (;;)
    {
        char op = *inPattern;

        switch (op)
        {
            case 0:
                if (*inName != 0)
                    return eM6NoMatchAndGreater;
                else
                    return eM6Match;
            case '*':
            {
                if (inPattern[1] == 0)    // last '*' matches all
                    return eM6Match;

                const char* n = inName;
                while (*n)
                {
                    if (M6Match(inPattern + 1, n) == eM6Match)
                        return eM6Match;
                    ++n;
                }
                return eM6NoMatchAndEqual;
            }
            case '?':
                if (*inName)
                {
                    if (M6Match(inPattern + 1, inName + 1) == eM6Match)
                        return eM6Match;
                    else
                        return eM6NoMatchAndEqual;
                }
                else
                    return eM6NoMatchAndLess;
            default:
                if (*inName == op)
                {
                    ++inName;
                    ++inPattern;
                }
                else
                {
                    int d = op - *inName;
                    if (d > 0)
                        return eM6NoMatchAndLess;
                    else
                        return eM6NoMatchAndGreater;
                }
                break;
        }
    }
}

static M6MatchResult M6Match(const char* inPattern, const char* inName, uint32 inLength)
{
    for (;;)
    {
        char op = *inPattern;

        switch (op)
        {
            case 0:
                if (inLength != 0)
                    return eM6NoMatchAndGreater;
                else
                    return eM6Match;
            case '*':
            {
                if (inPattern[1] == 0)    // last '*' matches all
                    return eM6Match;

                const char* n = inName;
                while (inLength > 0)
                {
                    if (M6Match(inPattern + 1, n, inLength) == eM6Match)
                        return eM6Match;

                    ++n; inLength--;
                }
                return eM6NoMatchAndEqual;
            }
            case '?':
                if (inLength > 0)
                {
                    if (M6Match(inPattern + 1, inName + 1, inLength - 1) == eM6Match)
                        return eM6Match;
                    else
                        return eM6NoMatchAndEqual;
                }
                else
                    return eM6NoMatchAndLess;
            default:
                if (*inName == op and inLength > 0)
                {
                    ++inName;
                    ++inPattern;
                    --inLength;
                }
                else
                {
                    char ch = 0;
                    if (inLength > 0)
                        ch = *inName;

                    int d = op - ch;
                    if (d > 0)
                        return eM6NoMatchAndLess;
                    else
                        return eM6NoMatchAndGreater;
                }
                break;
        }
    }
}

// --------------------------------------------------------------------
//    As stated above, the manipulation of data in the page is delegated
//    to a separate class.

template<class M6PageData>
class M6PageDataAccess
{
  public:
    typedef typename M6PageData::M6DataType        M6DataType;

    enum {
        kM6DataCount = M6PageData::kM6DataCount,
        kM6EntryCount = M6PageData::kM6EntryCount
    };

                    M6PageDataAccess(M6IndexPageData* inData);

    uint32            GetN() const                    { return mData.mN; }
    uint32            GetLink() const                    { return mData.mLink; }
    void            SetLink(uint32 inLink)            { mData.mLink = inLink; }

    uint32            Free() const;
    bool            CanStore(const string& inKey) const;
    bool            TooSmall() const                { return Free() > kM6MinKeySpace; }

    bool            BinarySearch(const string& inKey, int32& outIndex, M6IndexImpl& inIndex) const;

    string            GetKey(uint32 inIndex) const;
    M6DataType        GetValue(uint32 inIndex) const;

    template<class DataType = M6DataType>
    tuple<const char*, uint32, const DataType*>
                    Peek(uint32 inIndex) const;

    void            SetValue(uint32 inIndex, const M6DataType& inValue);
    void            InsertKeyValue(const string& inKey, const M6DataType& inValue, uint32 inIndex);
    void            GetKeyValue(uint32 inIndex, string& outKey, M6DataType& outValue) const;
    void            EraseEntry(uint32 inIndex);
    void            ReplaceKey(uint32 inIndex, const string& inKey);

    static void        MoveEntries(M6PageDataAccess& inFrom, M6PageDataAccess& inTo,
                        uint32 inFromOffset, uint32 inToOffset, uint32 inCount);

    const uint16*    GetKeyOffsets() const            { return mKeyOffsets; }

  private:
    M6PageData&        mData;
    uint16            mKeyOffsets[kM6EntryCount + 1];
};

template<class M6DataPage>
M6PageDataAccess<M6DataPage>::M6PageDataAccess(M6IndexPageData* inData)
    : mData(*reinterpret_cast<M6DataPage*>(inData))
{
    uint8* key = mData.mKeys;
    for (uint32 i = 0; i <= mData.mN; ++i)
    {
        assert(i <= kM6EntryCount);
        mKeyOffsets[i] = static_cast<uint16>(key - mData.mKeys);
        key += *key + 1;
    }
}

template<class M6DataPage>
uint32 M6PageDataAccess<M6DataPage>::Free() const
{
    return kM6DataCount * sizeof(M6DataType) - mKeyOffsets[mData.mN] - mData.mN * sizeof(M6DataType);
}

template<class M6DataPage>
bool M6PageDataAccess<M6DataPage>::CanStore(const string& inKey) const
{
    return mData.mN < kM6EntryCount and Free() >= (inKey.length() + 1 + sizeof(M6DataType));
}

//    Had to move the next function down since gcc is a single pass compiler?
//
//template<class M6DataPage>
//bool M6PageDataAccess<M6DataPage>::BinarySearch(const string& inKey, int32& outIndex, M6IndexImpl& inIndex) const
//{
//    bool match = false;
//
//    int32 L = 0, R = mData.mN - 1;
//    while (L <= R)
//    {
//        int32 i = (L + R) / 2;
//
//        const uint8* ko = mData.mKeys + mKeyOffsets[i];
//        const char* k = reinterpret_cast<const char*>(ko + 1);
//
//        int d = inIndex.CompareKeys(inKey.c_str(), inKey.length(), k, *ko);
//        if (d == 0)
//        {
//            outIndex = i;
//            match = true;
//            break;
//        }
//        else if (d < 0)
//            R = i - 1;
//        else
//            L = i + 1;
//    }
//
//    if (not match)
//        outIndex = R;
//
//    return match;
//}

template<class M6DataPage>
inline string M6PageDataAccess<M6DataPage>::GetKey(uint32 inIndex) const
{
    assert(inIndex < mData.mN);
    const uint8* key = mData.mKeys + mKeyOffsets[inIndex];
    return string(reinterpret_cast<const char*>(key) + 1, *key);
}

template<class M6DataPage>
inline typename M6PageDataAccess<M6DataPage>::M6DataType M6PageDataAccess<M6DataPage>::GetValue(uint32 inIndex) const
{
    assert(inIndex < mData.mN);
    return mData.mData[kM6DataCount - inIndex - 1];
}

template<class M6DataPage>
template<class DataType>
tuple<const char*, uint32, const DataType*>
M6PageDataAccess<M6DataPage>::Peek(uint32 inIndex) const
{
    assert(inIndex < mData.mN);

    const uint8* key = mData.mKeys + mKeyOffsets[inIndex];
    const M6DataType* data = &mData.mData[kM6DataCount - inIndex - 1];

    return make_tuple(reinterpret_cast<const char*>(key) + 1, static_cast<uint32>(*key), data);
}

template<class M6DataPage>
inline void M6PageDataAccess<M6DataPage>::SetValue(uint32 inIndex, const M6DataType& inValue)
{
    assert(inIndex < mData.mN);
    mData.mData[kM6DataCount - inIndex - 1] = inValue;
}

template<class M6DataPage>
inline void M6PageDataAccess<M6DataPage>::GetKeyValue(uint32 inIndex, string& outKey, M6DataType& outValue) const
{
    outKey = GetKey(inIndex);
    outValue = GetValue(inIndex);
}

template<class M6DataPage>
void M6PageDataAccess<M6DataPage>::InsertKeyValue(const string& inKey, const M6DataType& inValue, uint32 inIndex)
{
    assert(CanStore(inKey));
    assert(inIndex <= mData.mN);
    assert(mData.mN + 1 <= kM6EntryCount);

    if (inIndex < mData.mN)
    {
        void* src = mData.mKeys + mKeyOffsets[inIndex];
        void* dst = mData.mKeys + mKeyOffsets[inIndex] + inKey.length() + 1;

        // shift keys
        memmove(dst, src, mKeyOffsets[mData.mN] - mKeyOffsets[inIndex]);

        // shift data
        src = mData.mData + kM6DataCount - mData.mN;
        dst = mData.mData + kM6DataCount - mData.mN - 1;

        memmove(dst, src, (mData.mN - inIndex) * sizeof(M6DataType));
    }

    uint8* k = mData.mKeys + mKeyOffsets[inIndex];
    *k = static_cast<uint8>(inKey.length());
    memcpy(k + 1, inKey.c_str(), *k);
    mData.mData[kM6DataCount - inIndex - 1] = inValue;
    ++mData.mN;

    assert(mData.mN <= kM6EntryCount);

    // update key offsets
    for (uint32 i = inIndex + 1; i <= mData.mN; ++i)
        mKeyOffsets[i] = mKeyOffsets[i - 1] + mData.mKeys[mKeyOffsets[i - 1]] + 1;

    assert(mKeyOffsets[mData.mN] <= (kM6DataCount - mData.mN) * sizeof(M6DataType));
}

template<class M6DataPage>
void M6PageDataAccess<M6DataPage>::EraseEntry(uint32 inIndex)
{
    assert(inIndex < mData.mN);
    assert(mData.mN <= kM6EntryCount);

    if (mData.mN > inIndex + 1)
    {
        void* src = mData.mKeys + mKeyOffsets[inIndex + 1];
        void* dst = mData.mKeys + mKeyOffsets[inIndex];
        uint32 n = mKeyOffsets[mData.mN] - mKeyOffsets[inIndex + 1];
        memmove(dst, src, n);

        src = mData.mData + kM6DataCount - mData.mN;
        dst = mData.mData + kM6DataCount - mData.mN + 1;
        n = (mData.mN - inIndex - 1) * sizeof(M6DataType);
        memmove(dst, src, n);

        for (int i = inIndex + 1; i <= mData.mN; ++i)
            mKeyOffsets[i] = mKeyOffsets[i - 1] + mData.mKeys[mKeyOffsets[i - 1]] + 1;
    }

    --mData.mN;
}

template<class M6DataPage>
void M6PageDataAccess<M6DataPage>::ReplaceKey(uint32 inIndex, const string& inKey)
{
    assert(inIndex < mData.mN);
    assert(mData.mN <= kM6EntryCount);

    uint8* k = mData.mKeys + mKeyOffsets[inIndex];

    int32 delta = static_cast<int32>(inKey.length()) - *k;
    assert(delta < 0 or Free() >= static_cast<uint32>(delta));

    if (inIndex + 1 < mData.mN)
    {
        uint8* src = k + *k + 1;
        uint8* dst = src + delta;
        uint32 n = mKeyOffsets[mData.mN] - mKeyOffsets[inIndex + 1];
        memmove(dst, src, n);
    }

    *k = static_cast<uint8>(inKey.length());
    memcpy(k + 1, inKey.c_str(), inKey.length());

    for (int i = inIndex + 1; i <= mData.mN; ++i)
        mKeyOffsets[i] += delta;

    assert(mKeyOffsets[mData.mN] <= (kM6DataCount - mData.mN) * sizeof(M6DataType));
}

// move entries (keys and data) taking into account insertions and such
template<class M6DataPage>
void M6PageDataAccess<M6DataPage>::MoveEntries(M6PageDataAccess& inSrc, M6PageDataAccess& inDst,
    uint32 inSrcOffset, uint32 inDstOffset, uint32 inCount)
{
    assert(inSrcOffset <= inSrc.mData.mN);
    assert(inDstOffset <= inDst.mData.mN);
    assert(inDstOffset + inCount <= kM6DataCount);
    assert(inDst.mData.mN + inCount <= kM6EntryCount);

    // make room in dst first
    if (inDstOffset < inDst.mData.mN)
    {
        // make room in dst by shifting entries
        void* src = inDst.mData.mKeys + inDst.mKeyOffsets[inDstOffset];
        void* dst = inDst.mData.mKeys + inDst.mKeyOffsets[inDstOffset] +
            inSrc.mKeyOffsets[inSrcOffset + inCount] - inSrc.mKeyOffsets[inSrcOffset];
        uint32 n = inDst.mKeyOffsets[inDst.mData.mN] - inDst.mKeyOffsets[inDstOffset];
        memmove(dst, src, n);

        src = inDst.mData.mData + kM6DataCount - inDst.mData.mN;
        dst = inDst.mData.mData + kM6DataCount - inDst.mData.mN - inCount;
        memmove(dst, src, (inDst.mData.mN - inDstOffset) * sizeof(M6DataType));
    }

    // copy keys
    void* src = inSrc.mData.mKeys + inSrc.mKeyOffsets[inSrcOffset];
    void* dst = inDst.mData.mKeys + inDst.mKeyOffsets[inDstOffset];

    uint32 byteCount = inSrc.mKeyOffsets[inSrcOffset + inCount] -
                       inSrc.mKeyOffsets[inSrcOffset];

    assert(inSrc.mKeyOffsets[inSrcOffset] + byteCount <= kM6KeySpace);
    assert(byteCount + inCount * sizeof(M6DataType) <= inDst.Free());

    memcpy(dst, src, byteCount);

    // and data
    src = inSrc.mData.mData + kM6DataCount - inSrcOffset - inCount;
    dst = inDst.mData.mData + kM6DataCount - inDstOffset - inCount;
    byteCount = inCount * sizeof(M6DataType);
    memcpy(dst, src, byteCount);

    // and finally move remaining data in src
    if (inSrcOffset + inCount < inSrc.mData.mN)
    {
        void* src = inSrc.mData.mKeys + inSrc.mKeyOffsets[inSrcOffset + inCount];
        void* dst = inSrc.mData.mKeys + inSrc.mKeyOffsets[inSrcOffset];
        uint32 n = inSrc.mKeyOffsets[inSrc.mData.mN] - inSrc.mKeyOffsets[inSrcOffset + inCount];
        memmove(dst, src, n);

        src = inSrc.mData.mData + kM6DataCount - inSrc.mData.mN;
        dst = inSrc.mData.mData + kM6DataCount - inSrc.mData.mN + inCount;
        memmove(dst, src, (inSrc.mData.mN - inSrcOffset - inCount) * sizeof(M6DataType));
    }

    inDst.mData.mN += inCount;
    inSrc.mData.mN -= inCount;

    // update key offsets
    uint8* key = inSrc.mData.mKeys + inSrc.mKeyOffsets[inSrcOffset];
    for (int32 i = inSrcOffset; i <= inSrc.mData.mN; ++i)
    {
        inSrc.mKeyOffsets[i] = static_cast<uint16>(key - inSrc.mData.mKeys);
        key += *key + 1;
    }

    assert(inSrc.mKeyOffsets[inSrc.mData.mN] <= (kM6DataCount - inSrc.mData.mN) * sizeof(M6DataType));

    key = inDst.mData.mKeys + inDst.mKeyOffsets[inDstOffset];
    for (int32 i = inDstOffset; i <= inDst.mData.mN; ++i)
    {
        inDst.mKeyOffsets[i] = static_cast<uint16>(key - inDst.mData.mKeys);
        key += *key + 1;
    }

    assert(inDst.mKeyOffsets[inDst.mData.mN] <= (kM6DataCount - inDst.mData.mN) * sizeof(M6DataType));
}

template<class M6DataType>
struct M6IndexPageDataTypeFactory
{
};

template<>
struct M6IndexPageDataTypeFactory<M6IndexPageDataTraits<eM6IndexSimpleLeafPage>::M6DataElement>
{
    typedef M6IndexSimpleLeafPageData                M6LeafPageDataType;
    typedef M6PageDataAccess<M6LeafPageDataType>    M6DataAccess;
};

template<>
struct M6IndexPageDataTypeFactory<M6IndexPageDataTraits<eM6IndexMultiLeafPage>::M6DataElement>
{
    typedef M6IndexMultiLeafPageData                M6LeafPageDataType;
    typedef M6PageDataAccess<M6LeafPageDataType>    M6DataAccess;
};

template<>
struct M6IndexPageDataTypeFactory<M6IndexPageDataTraits<eM6IndexMultiIDLLeafPage>::M6DataElement>
{
    typedef M6IndexMultiIDLLeafPageData                M6LeafPageDataType;
    typedef M6PageDataAccess<M6LeafPageDataType>    M6DataAccess;
};

// --------------------------------------------------------------------

struct M6IxFileHeader
{
    uint32        mSignature;
    uint32        mHeaderSize;
    uint32        mSize;
    uint32        mDepth;
    uint32        mRoot;
    uint32        mFirstBitsPage;
    uint32        mLastBitsPage;
    uint32        mFirstLeafPage;
    uint32      mMaxWeight;
};

const uint32
    kM6IxFileHeaderV1Size = 32,
    kM6IxFileHeaderV2Size = sizeof(M6IxFileHeader);

union M6IxFileHeaderPage
{
    M6IxFileHeader    mHeader;
    uint8            mFiller[kM6IndexPageSize];
};

BOOST_STATIC_ASSERT(sizeof(M6IxFileHeaderPage) == kM6IndexPageSize);

// --------------------------------------------------------------------
//    M6BasicPage is the base class for all page classes

class M6BasicPage
{
  public:
                    M6BasicPage(M6IndexPageData* inData, uint32 inPageNr);
    virtual            ~M6BasicPage();

    void            Deallocate();
    void            Flush(M6File& inFile);

    uint32            GetPageNr() const                { return mPageNr; }
    void            SetPageNr(uint32 inPageNr)        { mPageNr = inPageNr; SetDirty(true); }

    virtual bool    IsDirty() const                    { return mDirty; }
    virtual void    SetDirty(bool inDirty)            { mDirty = inDirty; }

    uint32            GetN() const                    { return mData->mN; }
    void            SetLink(uint32 inLink)            { mData->mLink = inLink; SetDirty(true); }
    uint32            GetLink() const                    { return mData->mLink; }
    M6IndexPageKind    GetKind() const                    { return mData->mType; }

    void*            GetData()                        { return mData; }

  protected:
    M6IndexPageHeader*    mData;
    uint32                mPageNr;
    bool                mDirty;

  private:
                    M6BasicPage(const M6BasicPage&);
    M6BasicPage&    operator=(const M6BasicPage&);
};

// forward declarations for the four page types

template<class M6DataType>    class M6IndexPage;
template<class M6DataType>    class M6LeafPage;
template<class M6DataType>    class M6BranchPage;
                            class M6IndexBitVectorPage;

// --------------------------------------------------------------------

struct M6IndexImpl
{
                    M6IndexImpl(M6BasicIndex& inIndex, const fs::path& inPath,
                        M6IndexType inType, MOpenMode inMode);
    virtual         ~M6IndexImpl();

    void            StoreBits(M6OBitStream& inBits, M6BitVector& outBitVector);

    typedef M6BasicIndex::iterator    iterator;

    iterator        Begin();
    iterator        End();
    iterator        At(uint32 inPageNr, uint32 inKeyNr);

    virtual void    GetKey(uint32 inPage, uint32 inKeyNr, string& outKey) = 0;
    virtual bool    GetNextKey(uint32& ioPage, uint32& ioKeyNr, string& outKey) = 0;
    virtual uint32    GetCount(uint32 inPage, uint32 inKeyNr) = 0;

    virtual M6Iterator *GetIterator(uint32 inPage, uint32 inKeyNr) = 0;
    virtual void    Insert(uint32 inKey, const uint32& inValue)                        { THROW(("Incorrect use of index")); }
    virtual void    Insert(uint32 inKey, const M6MultiData& inValue)                { THROW(("Incorrect use of index")); }
    virtual void    Insert(uint32 inKey, const M6MultiIDLData& inValue)                { THROW(("Incorrect use of index")); }
    virtual void    Insert(const string& inKey, const uint32& inValue)                { THROW(("Incorrect use of index")); }
    virtual void    Insert(const string& inKey, const M6MultiData& inValue)            { THROW(("Incorrect use of index")); }
    virtual void    Insert(const string& inKey, const M6MultiIDLData& inValue)        { THROW(("Incorrect use of index")); }
    virtual bool    Erase(const string& inKey) = 0;
    virtual bool    Find(const string& inKey, uint32& outValue)                        { THROW(("Incorrect use of index")); }
    virtual bool    Find(const string& inKey, M6MultiData& outValue)                { THROW(("Incorrect use of index")); }
    virtual bool    Find(const string& inKey, M6MultiIDLData& outValue)                { THROW(("Incorrect use of index")); }
    virtual bool    Contains(const string& inKey) = 0;

    virtual M6Iterator*    Find(const string& inKey) = 0;
    virtual void        Find(const string& inKey, M6QueryOperator inOperator, vector<bool>& outBitmap, uint32& outCount) = 0;
    virtual void        Find(const string& inLowerBound, const string& inUpperBound, vector<bool>& outBitmap, uint32& outCount) = 0;
    virtual void        FindPattern(const string& inPattern, vector<bool>& outBitmap, uint32& outCount) = 0;
    virtual M6Iterator*    FindString(const string& inString) = 0;

    uint32            Size() const                { return mHeader.mSize; }
    uint32            Depth() const                { return mHeader.mDepth; }
    M6IndexType        GetIndexType() const        { return mIndexType; }

    uint32            GetMaxWeight() const        { return mHeader.mMaxWeight; }
    void            SetMaxWeight(uint32 inMaxWeight)
                                                { mHeader.mMaxWeight = inMaxWeight; }

    int                CompareKeys(const char* inKeyA, size_t inKeyLengthA,
                        const char* inKeyB, size_t inKeyLengthB) const
                    {
                        return mIndex.CompareKeys(inKeyA, inKeyLengthA, inKeyB, inKeyLengthB);
                    }

    int                CompareKeys(const string& inKeyA, const string& inKeyB) const
                    {
                        return mIndex.CompareKeys(inKeyA.c_str(), inKeyA.length(), inKeyB.c_str(), inKeyB.length());
                    }

    virtual void    GetBrowseSections(const string& inFirst, const string& inLast,
                        uint32 inNrOfSections, vector<pair<string,string>>& outSections) = 0;
    virtual void    GetBrowseEntries(const string& inFirst, const string& inLast,
                        vector<string>& outEntries) = 0;
    virtual void    VisitKeys(M6BasicIndex::KeyVisitor inVisitor) = 0;

    virtual void    Validate() = 0;
    virtual void    Dump() = 0;

    void            SetAutoCommit(bool inAutoCommit);
    virtual void    SetBatchMode(M6Lexicon& inLexicon) = 0;
    virtual void    FinishBatchMode(M6Progress& inProgress) = 0;
    virtual bool    IsInBatchMode() = 0;

    virtual void    Commit() = 0;
    virtual void    Rollback() = 0;
    virtual void    Vacuum(M6Progress& inProgress) = 0;

    // basic cache routines
    template<class PageType>    PageType*    Allocate();
    template<class PageType>    PageType*    Load(uint32 inPageNr);
    template<class PageType>    void        Reference(PageType* inPage);
    template<class PageType>    void        Release(PageType*& ioPage);
    void                                    SwapPages(uint32 inPageA, uint32 inPageB);

    virtual M6BasicPage*
                    GetFirstLeafPage() = 0;

  protected:

    virtual M6BasicPage*    CreateLeafPage(M6IndexPageData* inData, uint32 inPageNr) = 0;
    virtual M6BasicPage*    CreateBranchPage(M6IndexPageData* inData, uint32 inPageNr) = 0;

    fs::path        mPath;
    M6File            mFile;
    M6IndexType        mIndexType;
    M6BasicIndex&    mIndex;
    M6IxFileHeader    mHeader;
    bool            mAutoCommit;
    M6File*            mBatchFile;
    M6Lexicon*        mLexicon;
    bool            mDirty;

    // cache

    struct M6CachedPage;
    typedef M6CachedPage*    M6CachedPagePtr;

    struct M6CachedPage
    {
        uint32            mPageNr;
        M6BasicPage*    mPage;
        uint32            mRefCount;
        M6CachedPagePtr    mNext;
        M6CachedPagePtr    mPrev;
    };

    void            InitCache(uint32 inCacheCount);
    void            FlushCache();

    M6CachedPagePtr    GetCachePage();

    M6CachedPagePtr    mCache,    mLRUHead, mLRUTail;

    uint32            mCacheCount;
    const static uint32
                    kM6CacheCount = 16;
    boost::mutex    mCacheMutex;
};

template<class M6DataType>
class M6IndexImplT : public M6IndexImpl
{
  public:
    typedef M6IndexPage<M6DataType>            IndexPage;
    typedef M6LeafPage<M6DataType>            LeafPage;
    typedef M6BranchPage<M6DataType>        BranchPage;

                    M6IndexImplT(M6BasicIndex& inIndex, const fs::path& inPath,
                        M6IndexType inType, MOpenMode inMode);
    virtual         ~M6IndexImplT();

    virtual void    GetKey(uint32 inPage, uint32 inKeyNr, string& outKey);
    virtual bool    GetNextKey(uint32& ioPage, uint32& ioKeyNr, string& outKey);
    virtual M6Iterator*
                    GetIterator(uint32 inPage, uint32 inKeyNr);
    virtual M6Iterator*
                    GetIterator(const M6DataType& inValue);
    virtual uint32    AddHits(const M6DataType& inValue, vector<bool>& outBitmap);
    virtual uint32    GetCount(uint32 inPage, uint32 inKeyNr);

    virtual void    Insert(uint32 inKey, const M6DataType& inValue);
    virtual void    Insert(const string& inKey, const M6DataType& inValue);
    virtual bool    Erase(const string& inKey);
    virtual bool    Find(const string& inKey, M6DataType& outValue);

    virtual M6Iterator*    Find(const string& inKey);
    virtual void        Find(const string& inKey, M6QueryOperator inOperator, vector<bool>& outBitmap, uint32& outCount);
    virtual void        Find(const string& inLowerBound, const string& inUpperBound, vector<bool>& outBitmap, uint32& outCount);
    virtual void        FindPattern(const string& inPattern, vector<bool>& outBitmap, uint32& outCount);
    virtual M6Iterator*    FindString(const string& inString);

    virtual bool    Contains(const string& inKey);

    void            Remap(M6DataType& ioData, const vector<uint32>& inRemappedBitPageNrs);

    virtual void    Commit();
    virtual void    Rollback();
    virtual void    Vacuum(M6Progress& inProgress);

    virtual void    SetBatchMode(M6Lexicon& inLexicon);
    virtual void    FinishBatchMode(M6Progress& inProgress);
    virtual bool    IsInBatchMode()                 { return mBatchFile != nullptr; }
    void            CreateUpLevels(deque<pair<string,uint32>>& up);

    virtual void    Validate();
    virtual void    Dump();

    virtual M6BasicPage*
                    GetFirstLeafPage();

    // batch support
    struct M6BatchEntry
    {
        uint32        key;
        M6DataType    data;
    };

    virtual void    GetBrowseSections(const string& inFirst, const string& inLast,
                        uint32 inNrOfSections, vector<pair<string,string>>& outSections);
    virtual void    GetBrowseEntries(const string& inFirst, const string& inLast,
                        vector<string>& outEntries);
    virtual void    VisitKeys(M6BasicIndex::KeyVisitor inVisitor);

  protected:
    virtual M6BasicPage*    CreateLeafPage(M6IndexPageData* inData, uint32 inPageNr);
    virtual M6BasicPage*    CreateBranchPage(M6IndexPageData* inData, uint32 inPageNr);

    typedef function<bool(const char* inKey, uint32 inKeyLength, const M6DataType& inData)> Visitor;
    void            Visit(Visitor inVisitor, uint32 inPage = 0, uint32 inKey = 0);

    BOOST_STATIC_ASSERT(sizeof(M6BatchEntry[2]) == (2 * sizeof(M6BatchEntry)));

    M6BatchEntry*    mBatch;
    uint32            mBatchCount;

    void            FlushBatch();
};

// --------------------------------------------------------------------

M6BasicPage::M6BasicPage(M6IndexPageData* inData, uint32 inPageNr)
    : mData(&inData->branch)
    , mPageNr(inPageNr)
    , mDirty(false)
{
}

M6BasicPage::~M6BasicPage()
{
    assert(not IsDirty());
    delete mData;
}

void M6BasicPage::Deallocate()
{
    mDirty = true;
    mData->mType = eM6IndexEmptyPage;
}

void M6BasicPage::Flush(M6File& inFile)
{
    if (IsDirty())
    {
        inFile.PWrite(mData, kM6IndexPageSize, mPageNr * kM6IndexPageSize);
        SetDirty(false);
    }
}

// --------------------------------------------------------------------

template<class M6DataType>
class M6IndexPage : public M6BasicPage
{
  public:
//    typedef M6IndexPage<M6DataType>        IndexPage;
    typedef M6LeafPage<M6DataType>        LeafPage;
    typedef M6BranchPage<M6DataType>    BranchPage;

                        M6IndexPage(M6IndexPageData* inData, uint32 inPageNr)
                            : M6BasicPage(inData, inPageNr) {}

    virtual bool        IsLeaf() const = 0;

    virtual void        LowerBound(const string& inKey, uint32& outPage, uint32& outKeyNr) = 0;
    virtual bool        Find(const string& inKey, M6DataType& outValue) = 0;
    virtual bool        Insert(string& ioKey, const M6DataType& inValue, uint32& outLink) = 0;
    virtual bool        Erase(string& ioKey, int32 inIndex, BranchPage* inParent, BranchPage* inLinkPage, uint32 inLinkIndex) = 0;

    virtual void        Validate(const string& inKey, BranchPage* inParent) = 0;
    virtual void        Dump(int inLevel, BranchPage* inParent) = 0;
};

// --------------------------------------------------------------------

template<class M6DataType>
class M6LeafPage : public M6IndexPage<M6DataType>
{
  public:
    typedef ::M6IndexPage<M6DataType>    IndexPage;
    typedef ::M6LeafPage<M6DataType>    LeafPage;
    typedef ::M6BranchPage<M6DataType>    BranchPage;

    typedef typename M6IndexPageDataTypeFactory<M6DataType>::M6LeafPageDataType        M6DataPageType;
    typedef M6PageDataAccess<M6DataPageType>                                        M6Access;

    enum {
        kM6DataCount = M6DataPageType::kM6DataCount,
        kM6EntryCount = M6DataPageType::kM6EntryCount
    };

                        M6LeafPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr);

    // inline access to mAccess
    uint32                Free() const                                                { return mAccess.Free(); }
    bool                CanStore(const string& inKey) const                            { return mAccess.CanStore(inKey); }
    bool                TooSmall() const                                            { return mAccess.TooSmall(); }
    bool                BinarySearch(const string& inKey, int32& outIndex) const    { return mAccess.BinarySearch(inKey, outIndex, mIndex); }
    string                GetKey(uint32 inIndex) const                                { return mAccess.GetKey(inIndex); }
    M6DataType            GetValue(uint32 inIndex) const                                { return mAccess.GetValue(inIndex); }

    tuple<const char*, uint32, const M6DataType*>
                        Peek(uint32 inIndex) const                                    { return mAccess.Peek(inIndex); }

    void                SetValue(uint32 inIndex, const M6DataType& inValue)            { mAccess.SetValue(inIndex, inValue); this->mDirty = true; }
    void                InsertKeyValue(const string& inKey, const M6DataType& inValue, uint32 inIndex)
                                                                                    { mAccess.InsertKeyValue(inKey, inValue, inIndex); this->mDirty = true; }
    void                GetKeyValue(uint32 inIndex, string& outKey, M6DataType& outValue) const
                                                                                    { mAccess.GetKeyValue(inIndex, outKey, outValue); }
    void                EraseEntry(uint32 inIndex)                                    { mAccess.EraseEntry(inIndex); this->mDirty = true; }
    void                ReplaceKey(uint32 inIndex, const string& inKey)                { mAccess.ReplaceKey(inIndex, inKey); this->mDirty = true; }


    virtual bool        IsLeaf() const                                                { return true; }

    virtual void        LowerBound(const string& inKey, uint32& outPage, uint32& outKeyNr);
    virtual bool        Find(const string& inKey, M6DataType& outValue);
    virtual bool        Insert(string& ioKey, const M6DataType& inValue, uint32& outLink);
    virtual bool        Erase(string& ioKey, int32 inIndex, BranchPage* inParent, BranchPage* inLinkPage, uint32 inLinkIndex);

    virtual void        Validate(const string& inKey, BranchPage* inParent);
    virtual void        Dump(int inLevel, BranchPage* inParent);

  private:

    bool                Underflow(LeafPage& inRight, uint32 inIndex, BranchPage* inParent);

    M6IndexImpl&        mIndex;
    M6DataPageType*        mData;
    M6Access            mAccess;
    uint32                mPageNr;
};

// --------------------------------------------------------------------

template<class M6DataType>
class M6BranchPage : public M6IndexPage<M6DataType>
{
  public:
    typedef ::M6IndexPage<M6DataType>                IndexPage;
    typedef ::M6LeafPage<M6DataType>                LeafPage;
    typedef ::M6BranchPage<M6DataType>                BranchPage;

    typedef M6IndexBranchPageData                    M6DataPageType;
    typedef M6PageDataAccess<M6DataPageType>        M6Access;

    enum {
        kM6DataCount = M6DataPageType::kM6DataCount,
        kM6EntryCount = M6DataPageType::kM6EntryCount
    };

                        M6BranchPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr);

    uint32                Free() const                                                { return mAccess.Free(); }
    bool                CanStore(const string& inKey) const                            { return mAccess.CanStore(inKey); }
    bool                TooSmall() const                                            { return mAccess.TooSmall(); }
    bool                BinarySearch(const string& inKey, int32& outIndex) const    { return mAccess.BinarySearch(inKey, outIndex, mIndex); }
    string                GetKey(uint32 inIndex) const                                { return mAccess.GetKey(inIndex); }
    uint32                GetValue(uint32 inIndex) const                                { return mAccess.GetValue(inIndex); }

    tuple<const char*, uint32, const M6DataType*>
                        Peek(uint32 inIndex) const                                    { return mAccess.Peek(inIndex); }

    void                SetValue(uint32 inIndex, uint32 inValue)                    { mAccess.SetValue(inIndex, inValue); this->mDirty = true; }
    void                InsertKeyValue(const string& inKey, uint32 inValue, uint32 inIndex)
                                                                                    { mAccess.InsertKeyValue(inKey, inValue, inIndex); this->mDirty = true; }
    void                GetKeyValue(uint32 inIndex, string& outKey, uint32& outValue) const
                                                                                    { mAccess.GetKeyValue(inIndex, outKey, outValue); }
    void                EraseEntry(uint32 inIndex)                                    { mAccess.EraseEntry(inIndex); this->mDirty = true; }
    void                ReplaceKey(uint32 inIndex, const string& inKey)                { mAccess.ReplaceKey(inIndex, inKey); this->mDirty = true; }

    virtual bool        IsLeaf() const                                                { return false; }

    virtual void        LowerBound(const string& inKey, uint32& outPage, uint32& outKeyNr);
    virtual bool        Find(const string& inKey, M6DataType& outValue);
    virtual bool        Insert(string& ioKey, const M6DataType& inValue, uint32& outLink);
    virtual bool        Erase(string& ioKey, int32 inIndex, BranchPage* inParent, BranchPage* inLinkPage, uint32 inLinkIndex);

    virtual void        Validate(const string& inKey, BranchPage* inParent);
    virtual void        Dump(int inLevel, BranchPage* inParent);

  private:

    bool                Underflow(BranchPage& inRight, uint32 inIndex, BranchPage* inParent);

    M6IndexImpl&        mIndex;
    M6DataPageType*        mData;
    M6Access            mAccess;
    uint32                mPageNr;
};

// --------------------------------------------------------------------

template<class M6DataType>
M6LeafPage<M6DataType>::M6LeafPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
    : M6IndexPage<M6DataType>(inData, inPageNr)
    , mIndex(inIndexImpl)
    , mData(reinterpret_cast<M6DataPageType*>(inData))
    , mAccess(inData)
    , mPageNr(inPageNr)
{
}

template<class M6DataType>
void M6LeafPage<M6DataType>::LowerBound(const string& inKey, uint32& outPageNr, uint32& outKeyNr)
{
    outPageNr = mPageNr;
    int32 ix;

    (void)BinarySearch(inKey, ix);
    if (ix < 0)
        outKeyNr = 0;
    else
        outKeyNr = ix;
}

template<class M6DataType>
bool M6LeafPage<M6DataType>::Find(const string& inKey, M6DataType& outValue)
{
    int32 ix;
    bool result = false;

    if (BinarySearch(inKey, ix))
    {
        result = true;
        outValue = GetValue(ix);
    }

    return result;
}

template<class M6DataType>
bool M6LeafPage<M6DataType>::Insert(string& ioKey, const M6DataType& inValue, uint32& outLink)
{
    bool result = false;

    int32 ix;

    if (BinarySearch(ioKey, ix))
        throw M6DuplicateKeyException(ioKey);
//        SetValue(ix, inValue);    // simply update the value (we're a unique index)
    else if (CanStore(ioKey))
        InsertKeyValue(ioKey, inValue, ix + 1);
    else
    {
        ix += 1;    // we need to insert at ix + 1

        // create a new leaf page
        LeafPage* next(mIndex.Allocate<LeafPage>());

        uint32 split = mData->mN / 2;

        mAccess.MoveEntries(mAccess, next->mAccess, split, 0, mData->mN - split);
        this->SetDirty(true);
        next->SetDirty(true);

        next->mData->mLink = mData->mLink;
        mData->mLink = next->GetPageNr();

        if (ix <= mData->mN)
            InsertKeyValue(ioKey, inValue, ix);
        else
            next->InsertKeyValue(ioKey, inValue, ix - mData->mN);

        ioKey = next->GetKey(0);
        outLink = next->GetPageNr();

        mIndex.Release(next);

        result = true;
    }

    return result;
}

template<class M6DataType>
bool M6LeafPage<M6DataType>::Erase(string& ioKey, int32 inIndex, BranchPage* inParent, BranchPage* inLinkPage, uint32 inLinkIndex)
{
    bool result = false;
    int32 ix;

    if (BinarySearch(ioKey, ix))        // match in a leaf page
    {
        result = true;

        // erase the key at ix
        EraseEntry(ix);

        if (inParent != nullptr)
        {
            assert(mData->mN > 0);
            if (ix == 0 and mData->mN > 0 and inLinkPage != nullptr)    // need to pass on the new key
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
            {                            // we're not the root page and we've lost too many entries
                if (inIndex + 1 < static_cast<int32>(inParent->GetN()))
                {
                    // try to compensate using our right sibling
                    LeafPage* right(mIndex.Load<LeafPage>(inParent->GetValue(inIndex + 1)));
                    Underflow(*right, inIndex + 1, inParent);
                    mIndex.Release(right);
                }

                if (TooSmall() and inIndex >= 0)
                {
                    // if still too small, try with the left sibling
                    uint32 leftNr;
                    if (inIndex > 0)
                        leftNr = inParent->GetValue(inIndex - 1);
                    else
                        leftNr = inParent->GetLink();

                    LeafPage* left(mIndex.Load<LeafPage>(leftNr));
                    left->Underflow(*this, inIndex, inParent);
                    mIndex.Release(left);
                }

                result = true;
            }
        }
    }

    return result;
}

template<class M6DataType>
bool M6LeafPage<M6DataType>::Underflow(LeafPage& inRight, uint32 inIndex, BranchPage* inParent)
{
    // Page left of right contains too few entries, see if we can fix this
    // first try a merge
    if (Free() + inRight.Free() >= kM6KeySpace and
        mData->mN + inRight.mData->mN <= kM6EntryCount)
    {
        // join the pages
        mAccess.MoveEntries(inRight.mAccess, mAccess, 0, mData->mN, inRight.mData->mN);
        this->SetLink(inRight.GetLink());

        inParent->EraseEntry(inIndex);
        inRight.Deallocate();

        this->SetDirty(true);
    }
    else        // redistribute the data
    {
        // pKey is the key in inParent at inIndex (and, since this a leaf, the first key in inRight)
        string pKey = inParent->GetKey(inIndex);
        assert(pKey == inRight.GetKey(0));
        int32 pKeyLen = static_cast<int32>(pKey.length());
        int32 pFree = inParent->Free();

        if (Free() > inRight.Free() and mData->mN < kM6EntryCount)    // move items from inRight to left
        {
            assert(TooSmall());

            int32 delta = Free() - inRight.Free();
            int32 needed = delta / 2;

            uint8* rk = inRight.mData->mKeys;
            uint32 n = 0, ln = 0;
            while (n < inRight.mData->mN and n + mData->mN < kM6EntryCount and needed > *rk)
            {
                ++n;
                if ((*rk - pKeyLen + pFree) > 0)    // if the new first key of inRight fits in the parent
                    ln = n;                            // we have a candidate
                needed -= *rk + sizeof(M6DataType);
                rk += *rk + 1;
            }

            // move the data
            mAccess.MoveEntries(inRight.mAccess, mAccess, 0, mData->mN, ln);
            inParent->ReplaceKey(inIndex, inRight.GetKey(0));

            this->SetDirty(true);
        }
        else if (inRight.Free() > Free() and inRight.mData->mN < kM6EntryCount)
        {
            assert(inRight.TooSmall());

            int32 delta = inRight.Free() - Free();
            int32 needed = delta / 2;

            const uint16* keyOffsets = mAccess.GetKeyOffsets();

            uint8* rk = mData->mKeys + keyOffsets[mData->mN - 1];
            uint32 n = 0, ln = 0;
            while (n < mData->mN and n + inRight.mData->mN < kM6EntryCount and needed > *rk)
            {
                ++n;
                if ((*rk - pKeyLen + pFree) > 0)    // if the new first key of inRight fits in the parent
                    ln = n;                            // we have a candidate
                needed -= *rk + sizeof(M6DataType);
                rk = mData->mKeys + keyOffsets[mData->mN - 1 - n];
            }

            // move the data
            mAccess.MoveEntries(mAccess, inRight.mAccess, mData->mN - ln, 0, ln);
            inParent->ReplaceKey(inIndex, inRight.GetKey(0));
            this->SetDirty(true);
        }
    }

    return not (TooSmall() or inRight.TooSmall());
}

template<class M6DataType>
void M6LeafPage<M6DataType>::Validate(const string& inKey, BranchPage* inParent)
{
//    M6VALID_ASSERT(mPageData.mN >= kM6MinEntriesPerPage or inParent == nullptr);
    //M6VALID_ASSERT(inParent == nullptr or not TooSmall());
    M6VALID_ASSERT(inKey.empty() or GetKey(0) == inKey);

    for (uint32 i = 0; i < mData->mN; ++i)
    {
        if (i > 0)
        {
//            M6VALID_ASSERT(GetValue(i) > GetValue(i - 1));
            M6VALID_ASSERT(mIndex.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
        }
    }

    if (mData->mLink != 0)
    {
        LeafPage* next(mIndex.Load<LeafPage>(mData->mLink));
        M6VALID_ASSERT(mIndex.CompareKeys(GetKey(mData->mN - 1), next->GetKey(0)) < 0);
        mIndex.Release(next);
    }
}

template<class M6DataType>
void M6LeafPage<M6DataType>::Dump(int inLevel, BranchPage* inParent)
{
    string prefix(inLevel * 2, ' ');

    cout << prefix << "leaf page at " << mPageNr << "; N = " << mData->mN << ": [" << endl;
    for (int i = 0; i < mData->mN; ++i)
    {
        cout << prefix << "  " << GetKey(i) << '(' << GetValue(i) << ')'
             << (i + 1 < mData->mN ? ", " : "") << endl;

//        M6IBitStream bits(new M6IBitVectorImpl(*this, d.mBitVector));
//        vector<uint32> docs;
//        ReadArray(bits, docs);
//
//        os << '[';
//        bool first = true;
//        for (uint32 doc : docs)
//        {
//            if (not first) os << ", ";
//            first = false;
//            os << doc;
//        }
//
//        os << ']';
    }
    cout << prefix << "]" << endl;

    if (mData->mLink)
    {
        LeafPage* next(mIndex.Load<LeafPage>(mData->mLink));
        cout << prefix << "  " << "link: " << next->GetKey(0) << endl;
        mIndex.Release(next);
    }
}

// --------------------------------------------------------------------

template<class M6DataType>
M6BranchPage<M6DataType>::M6BranchPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
    : M6IndexPage<M6DataType>(inData, inPageNr)
    , mIndex(inIndexImpl)
    , mData(&inData->branch)
    , mAccess(inData)
    , mPageNr(inPageNr)
{
}

template<class M6DataType>
void M6BranchPage<M6DataType>::LowerBound(const string& inKey, uint32& outPageNr, uint32& outKeyNr)
{
    int32 ix;

    BinarySearch(inKey, ix);

    if (ix < 0)
        outPageNr = mData->mLink;
    else
        outPageNr = GetValue(ix);

    IndexPage* page(mIndex.Load<IndexPage>(outPageNr));
    page->LowerBound(inKey, outPageNr, outKeyNr);
    mIndex.Release(page);
}

template<class M6DataType>
bool M6BranchPage<M6DataType>::Find(const string& inKey, M6DataType& outValue)
{
    int32 ix;

    BinarySearch(inKey, ix);

    uint32 pageNr;

    if (ix < 0)
        pageNr = mData->mLink;
    else
        pageNr = GetValue(ix);

    IndexPage* page(mIndex.Load<IndexPage>(pageNr));
    bool result = page->Find(inKey, outValue);
    mIndex.Release(page);
    return result;
}

/*
    Insert returns a bool indicating the depth increased.
    In that case the ioKey and ioValue are updated to the values
    to be inserted in the calling page (or a new root has to be made).
*/

template<class M6DataType>
bool M6BranchPage<M6DataType>::Insert(string& ioKey, const M6DataType& inValue, uint32& outLink)
{
    bool result = false;
    int32 ix;

    BinarySearch(ioKey, ix);

    uint32 pageNr;

    if (ix < 0)
        pageNr = mData->mLink;
    else
        pageNr = GetValue(ix);

    IndexPage* page(mIndex.Load<IndexPage>(pageNr));

    uint32 link;
    if (page->Insert(ioKey, inValue, link))
    {
        // page was split, we now need to store ioKey in our page
        ix += 1;    // we need to insert at ix + 1

        if (CanStore(ioKey))
            InsertKeyValue(ioKey, link, ix);
        else
        {
            // the key needs to be inserted but it didn't fit
            // so we need to split the page

            BranchPage* next(mIndex.Allocate<BranchPage>());

            int32 split = mData->mN / 2;
            string upKey;
            uint32 downPage;

            if (ix == split)
            {
                upKey = ioKey;
                downPage = link;

                mAccess.MoveEntries(mAccess, next->mAccess, split, 0, mData->mN - split);
                this->SetDirty(true);
            }
            else if (ix < split)
            {
                --split;
                GetKeyValue(split, upKey, downPage);
                mAccess.MoveEntries(mAccess, next->mAccess, split + 1, 0, mData->mN - split - 1);
                mData->mN -= 1;

                if (ix <= split)
                    InsertKeyValue(ioKey, link, ix);
                else
                    next->InsertKeyValue(ioKey, link, ix - split - 1);

                this->SetDirty(true);
                next->SetDirty(true);
            }
            else
            {
                upKey = GetKey(split);
                downPage = GetValue(split);

                mAccess.MoveEntries(mAccess, next->mAccess, split + 1, 0, mData->mN - split - 1);
                mData->mN -= 1;

                if (ix < split)
                    InsertKeyValue(ioKey, link, ix);
                else
                    next->InsertKeyValue(ioKey, link, ix - split - 1);

                this->SetDirty(true);
                next->SetDirty(true);
            }

            next->SetLink(downPage);

            ioKey = upKey;
            outLink = next->GetPageNr();

            mIndex.Release(next);

            result = true;
        }
    }

//    assert(mData->mN >= kM6MinEntriesPerPage or inParent == nullptr);
    assert(mData->mN <= kM6DataCount);

    mIndex.Release(page);

    return result;
}

template<class M6DataType>
bool M6BranchPage<M6DataType>::Erase(string& ioKey, int32 inIndex, BranchPage* inParent, BranchPage* inLinkPage, uint32 inLinkIndex)
{
    bool result = false;
    int32 ix;

    if (BinarySearch(ioKey, ix))
    {
        assert(inLinkPage == nullptr);
        inLinkPage = this;
        inLinkIndex = ix;
    }

    uint32 pageNr;

    if (ix < 0)
        pageNr = mData->mLink;
    else
        pageNr = GetValue(ix);

    IndexPage* page(mIndex.Load<IndexPage>(pageNr));
    if (page->Erase(ioKey, ix, this, inLinkPage, inLinkIndex))
    {
        result = true;

        if (TooSmall() and inParent != nullptr)
        {
            if (inIndex + 1 < static_cast<int32>(inParent->GetN()))
            {
                // try to compensate using our right sibling
                BranchPage* right(mIndex.Load<BranchPage>(inParent->GetValue(inIndex + 1)));
                Underflow(*right, inIndex + 1, inParent);
                mIndex.Release(right);
            }

            if (TooSmall() and inIndex >= 0)
            {
                // if still too small, try with the left sibling
                BranchPage* left(mIndex.Load<BranchPage>(inIndex > 0 ? inParent->GetValue(inIndex - 1) : inParent->mData->mLink));
                left->Underflow(*this, inIndex, inParent);
                mIndex.Release(left);
            }
        }
    }

    mIndex.Release(page);

    return result;
}

template<class M6DataType>
bool M6BranchPage<M6DataType>::Underflow(BranchPage& inRight, uint32 inIndex, BranchPage* inParent)
{
    // This page left of inRight contains too few entries, see if we can fix this
    // first try a merge

    // pKey is the key in inParent at inIndex (and, since this a leaf, the first key in inRight)
    string pKey = inParent->GetKey(inIndex);
    int32 pKeyLen = static_cast<int32>(pKey.length());

    if (Free() + inRight.Free() - pKeyLen - sizeof(M6DataType) >= kM6KeySpace and
        mData->mN + inRight.mData->mN + 1 <= kM6EntryCount)
    {
        InsertKeyValue(pKey, inRight.mData->mLink, mData->mN);

        // join the pages
        mAccess.MoveEntries(inRight.mAccess, mAccess, 0, mData->mN, inRight.mData->mN);

        inParent->EraseEntry(inIndex);
        inRight.Deallocate();
    }
    else        // redistribute the data
    {
        if (Free() > inRight.Free() and mData->mN < kM6EntryCount)    // rotate an entry from right to left
        {                                    // but only if it fits in the parent
            string rKey = inRight.GetKey(0);
            int32 delta = static_cast<int32>(rKey.length() - pKey.length());
            if (delta <= static_cast<int32>(inParent->Free()))
            {
                InsertKeyValue(pKey, inRight.mData->mLink, mData->mN);
                inParent->ReplaceKey(inIndex, rKey);
                inParent->SetValue(inIndex, inRight.mPageNr);
                inRight.mData->mLink = inRight.GetValue(0);
                inRight.EraseEntry(0);
            }
        }
        else if (inRight.Free() > Free() and inRight.mData->mN < kM6EntryCount)
        {
            string lKey = GetKey(mData->mN - 1);
            int32 delta = static_cast<int32>(lKey.length() - pKey.length());
            if (delta <= static_cast<int32>(inParent->Free()))
            {
                inRight.InsertKeyValue(pKey, inRight.mData->mLink, 0);
                inRight.mData->mLink = GetValue(mData->mN - 1);
                inParent->ReplaceKey(inIndex, lKey);
                EraseEntry(mData->mN - 1);
            }
        }
    }

    return not (TooSmall() or inRight.TooSmall());
}

template<class M6DataType>
void M6BranchPage<M6DataType>::Validate(const string& inKey, BranchPage* inParent)
{
//        M6VALID_ASSERT(mData->mN >= kM6MinEntriesPerPage or inParent == nullptr);
    //M6VALID_ASSERT(inParent == nullptr or not TooSmall());

    for (uint32 i = 0; i < mData->mN; ++i)
    {
        IndexPage* link(mIndex.Load<IndexPage>(mData->mLink));
        link->Validate(inKey, this);
        mIndex.Release(link);

        for (uint32 i = 0; i < mData->mN; ++i)
        {
            IndexPage* page(mIndex.Load<IndexPage>(GetValue(i)));
            page->Validate(GetKey(i), this);
            mIndex.Release(page);
            if (i > 0)
                M6VALID_ASSERT(mIndex.CompareKeys(GetKey(i - 1), GetKey(i)) < 0);
        }
    }
}

template<class M6DataType>
void M6BranchPage<M6DataType>::Dump(int inLevel, BranchPage* inParent)
{
    string prefix(inLevel * 2, ' ');

    cout << prefix << (inParent ? "branch" : "root") << " page at " << mPageNr << "; N = " << mData->mN << ": {";
    for (int i = 0; i < mData->mN; ++i)
        cout << GetKey(i) << (i + 1 < mData->mN ? ", " : "");
    cout << "}" << endl;

    IndexPage* link(mIndex.Load<IndexPage>(mData->mLink));
    link->Dump(inLevel + 1, this);
    mIndex.Release(link);

    for (int i = 0; i < mData->mN; ++i)
    {
        cout << prefix << inLevel << '.' << i << ") " << GetKey(i) << endl;

        IndexPage* sub(mIndex.Load<IndexPage>(GetValue(i)));
        sub->Dump(inLevel + 1, this);
        mIndex.Release(sub);
    }
}

// --------------------------------------------------------------------

class M6IndexBitVectorPage : public M6BasicPage
{
  public:

    typedef M6IndexBitVectorPageData                    M6DataPageType;

                    M6IndexBitVectorPage(M6IndexImpl& inIndexImpl, M6IndexPageData* inData, uint32 inPageNr)
                        : M6BasicPage(inData, inPageNr)
                        , mPageData(inData->bit_vector)
                    {
                        mPageData.mType = M6IndexBitVectorPageData::kIndexPageType;
                    }

    uint32            StoreBitVector(const uint8* inData, size_t inSize);

    uint8*            GetData(uint32 inOffset)        { return mPageData.mBits + inOffset; }

  private:
    M6IndexBitVectorPageData&    mPageData;
};

uint32 M6IndexBitVectorPage::StoreBitVector(const uint8* inData, size_t inSize)
{
    size_t result = kM6KeySpace - mPageData.mN;
    if (result > inSize)
        result = inSize;

    memcpy(mPageData.mBits + mPageData.mN, inData, result);

    mPageData.mN += static_cast<uint16>(result);
    SetDirty(true);

    return static_cast<uint32>(result);
}

struct M6IBitVectorImpl : public M6IBitStreamImpl
{
                    M6IBitVectorImpl(M6IndexImpl& inIndex, const M6BitVector& inBitVector);
                    M6IBitVectorImpl(const M6IBitVectorImpl& inImpl);
                    ~M6IBitVectorImpl();

    M6IBitVectorImpl&
                    operator=(const M6IBitVectorImpl&);

    virtual M6IBitStreamImpl*    Clone()        { return new M6IBitVectorImpl(*this); }
    virtual void                Read();

  private:
    M6IndexImpl&    mIndex;
    uint32            mPageNr;
    uint16            mOffset;
    uint8            mData[kM6BitVectorInlineBitsSize];
    M6IndexBitVectorPage*
                    mPage;
};

M6IBitVectorImpl::M6IBitVectorImpl(M6IndexImpl& inIndex, const M6BitVector& inBitVector)
    : mIndex(inIndex)
    , mPageNr(0)
    , mOffset(0)
    , mPage(nullptr)
{
    if (inBitVector.mType == eM6InlineBitVector)
    {
        memcpy(mData, inBitVector.mBits, kM6BitVectorInlineBitsSize);
        mBufferPtr = mData;
        mBufferSize = kM6BitVectorInlineBitsSize;
    }
    else
    {
        mPageNr = inBitVector.mPage;
        mOffset = inBitVector.mOffset;

        memcpy(mData, inBitVector.mLeadingBits, kM6BitVectorLeadingBitsSize);
        mBufferPtr = mData;
        mBufferSize = kM6BitVectorLeadingBitsSize;
    }
}

M6IBitVectorImpl::M6IBitVectorImpl(const M6IBitVectorImpl& inImpl)
    : M6IBitStreamImpl(inImpl)
    , mIndex(inImpl.mIndex)
    , mPageNr(inImpl.mPageNr)
    , mOffset(inImpl.mOffset)
    , mPage(inImpl.mPage)
{
    if (mPage != nullptr)
        mIndex.Reference(mPage);
    else if (inImpl.mBufferPtr >= inImpl.mData and inImpl.mBufferPtr < inImpl.mData + kM6BitVectorInlineBitsSize)
    {
        memcpy(mData, inImpl.mBufferPtr, inImpl.mBufferSize);
        mBufferPtr = mData;
        mBufferSize = inImpl.mBufferSize;
    }
}

M6IBitVectorImpl::~M6IBitVectorImpl()
{
    if (mPage != nullptr)
        mIndex.Release(mPage);
}

void M6IBitVectorImpl::Read()
{
    if (mPage != nullptr)
    {
        mIndex.Release(mPage);
        mPage = nullptr;
    }

    if (mPageNr != 0)
    {
        mPage = mIndex.Load<M6IndexBitVectorPage>(mPageNr);
        mBufferPtr = mPage->GetData(mOffset);
        mBufferSize = kM6KeySpace - mOffset;

        mPageNr = mPage->GetLink();
        mOffset = 0;
    }
}

// --------------------------------------------------------------------
// BinarySearch function moved here because of gcc problems

template<class M6DataPage>
bool M6PageDataAccess<M6DataPage>::BinarySearch(const string& inKey, int32& outIndex, M6IndexImpl& inIndex) const
{
    bool result = false;

    int32 L = 0, R = mData.mN - 1;
    while (L <= R)
    {
        int32 i = (L + R) / 2;

        const uint8* ko = mData.mKeys + mKeyOffsets[i];
        const char* k = reinterpret_cast<const char*>(ko + 1);

        int d = inIndex.CompareKeys(inKey.c_str(), inKey.length(), k, *ko);
        if (d == 0)
        {
            outIndex = i;
            result = true;
            break;
        }
        else if (d < 0)
            R = i - 1;
        else
            L = i + 1;
    }

    if (not result)
        outIndex = R;

    return result;
}

// --------------------------------------------------------------------

M6IndexImpl::M6IndexImpl(M6BasicIndex& inIndex, const fs::path& inPath, M6IndexType inType, MOpenMode inMode)
    : mPath(inPath)
    , mFile(inPath, inMode)
    , mIndexType(inType)
    , mIndex(inIndex)
    , mAutoCommit(true)
    , mBatchFile(nullptr)
    , mLexicon(nullptr)
    , mDirty(false)
    , mCache(nullptr), mLRUHead(nullptr), mLRUTail(nullptr), mCacheCount(0)
{
    if (inMode == eReadWrite and mFile.Size() == 0)
    {
        M6IxFileHeaderPage page = {};
        page.mHeader.mSignature = inType;
        page.mHeader.mHeaderSize = sizeof(M6IxFileHeader);
        page.mHeader.mMaxWeight = kM6MaxWeight;
        mFile.PWrite(&page, kM6IndexPageSize, 0);

        mHeader = page.mHeader;
        mHeader.mDepth = 0;
        mDirty = true;

        mFile.PWrite(mHeader, 0);
    }
    else
        mFile.PRead(mHeader, 0);

    assert(mHeader.mSignature == mIndexType);

    if (mHeader.mHeaderSize == kM6IxFileHeaderV1Size)   // backward compatible
        mHeader.mMaxWeight = kM6MaxWeight;
    else
        assert(mHeader.mHeaderSize == kM6IxFileHeaderV2Size);
}

M6IndexImpl::~M6IndexImpl()
{
    boost::unique_lock<boost::mutex> lock(mCacheMutex);

    FlushCache();
    delete [] mCache;

    if (mDirty)
        mFile.PWrite(mHeader, 0);

    delete mBatchFile;
}

void M6IndexImpl::StoreBits(M6OBitStream& inBits, M6BitVector& outBitVector)
{
    inBits.Sync();
    const uint8* data = inBits.Peek();
    size_t size = inBits.Size();

//cout << endl;
//HexDump(data, size, cout);
//cout << endl;

    if (size <= kM6BitVectorInlineBitsSize)
    {
        outBitVector.mType = eM6InlineBitVector;
        memcpy(outBitVector.mBits, data, size);
    }
    else
    {
        M6IndexBitVectorPage* page;
        if (mHeader.mLastBitsPage == 0)
        {
            page = Allocate<M6IndexBitVectorPage>();
            mHeader.mFirstBitsPage = mHeader.mLastBitsPage = page->GetPageNr();
        }
        else
            page = Load<M6IndexBitVectorPage>(mHeader.mLastBitsPage);

        if (kM6KeySpace - page->GetN() == 0)
        {
            M6IndexBitVectorPage* next = Allocate<M6IndexBitVectorPage>();
            mHeader.mLastBitsPage = next->GetPageNr();
            page->SetLink(mHeader.mLastBitsPage);
            Release(page);
            page = next;
        }

        outBitVector.mType = eM6OnDiskBitVector;
        outBitVector.mOffset = page->GetN();
        outBitVector.mPage = page->GetPageNr();

        memcpy(outBitVector.mLeadingBits, data, kM6BitVectorLeadingBitsSize);
        data += kM6BitVectorLeadingBitsSize;
        size -= kM6BitVectorLeadingBitsSize;

        for (;;)
        {
            uint32 n = page->StoreBitVector(data, size);

            data += n;
            size -= n;

            if (size == 0)
                break;

            M6IndexBitVectorPage* next = Allocate<M6IndexBitVectorPage>();
            mHeader.mLastBitsPage = next->GetPageNr();
            page->SetLink(mHeader.mLastBitsPage);
            Release(page);
            page = next;
        }
        Release(page);
    }
}

void M6IndexImpl::InitCache(uint32 inCacheCount)
{
    assert(inCacheCount > mCacheCount);

    M6CachedPagePtr tmp = new M6CachedPage[inCacheCount];

    for (uint32 ix = 0; ix < inCacheCount; ++ix)
    {
        tmp[ix].mPage = nullptr;
        tmp[ix].mPageNr = 0;
        tmp[ix].mRefCount = 0;
        tmp[ix].mNext = tmp + ix + 1;
        tmp[ix].mPrev = tmp + ix - 1;
    }

    tmp[0].mPrev = tmp[inCacheCount - 1].mNext = nullptr;

    for (M6CachedPagePtr a = mLRUHead, b = tmp; a != nullptr and b != nullptr; a = a->mNext, b = b->mNext)
    {
        b->mPageNr = a->mPageNr;
        b->mPage = a->mPage;
        b->mRefCount = a->mRefCount;
    }

    delete[] mCache;
    mCache = tmp;
    mCacheCount = inCacheCount;

    mLRUHead = mCache;
    mLRUTail = mCache + mCacheCount - 1;
}

void M6IndexImpl::FlushCache()
{
    for (uint32 ix = 0; ix < mCacheCount; ++ix)
    {
        if (mCache[ix].mPage != nullptr)
        {
            if (mCache[ix].mPage->IsDirty())
                mCache[ix].mPage->Flush(mFile);
            delete mCache[ix].mPage;
            mCache[ix].mPage = nullptr;
        }
        mCache[ix].mPageNr = 0;
    }
}

M6IndexImpl::M6CachedPagePtr M6IndexImpl::GetCachePage()
{
    M6CachedPagePtr result = mLRUTail;

    // now search backwards for a cached page that can be recycled
    uint32 n = 0;
    while (result != nullptr and result->mRefCount > 0)
    {
        result = result->mPrev;
        ++n;
    }

    // we could end up with a full cache, if so, double the cache
    if (result == nullptr)
    {
        InitCache(mCacheCount * 2);
        result = mLRUTail;
    }

    if (result != mLRUHead)
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

    if (result->mPage != nullptr)
    {
        if (result->mPage->IsDirty())
            result->mPage->Flush(mFile);

        delete result->mPage;
        result->mPage = nullptr;
    }

    return result;
}

template<class Page>
Page* M6IndexImpl::Allocate()
{
    boost::unique_lock<boost::mutex> lock(mCacheMutex);

    int64 fileSize = mFile.Size();
    uint32 pageNr = static_cast<uint32>((fileSize - 1) / kM6IndexPageSize + 1);
    int64 offset = pageNr * kM6IndexPageSize;
    mFile.Truncate(offset + kM6IndexPageSize);

    M6IndexPageData* data = new M6IndexPageData;
    memset(data, 0, kM6IndexPageSize);
    data->leaf.mType = Page::M6DataPageType::kIndexPageType;

    Page* page = new Page(*this, data, pageNr);
    page->SetDirty(true);
    page->Flush(mFile);

    M6CachedPagePtr cp = GetCachePage();
    cp->mPage = page;
    cp->mPageNr = pageNr;
    cp->mRefCount = 1;

    return page;
}

template<class Page>
Page* M6IndexImpl::Load(uint32 inPageNr)
{
    if (inPageNr == 0)
        THROW(("Invalid page number"));

    boost::unique_lock<boost::mutex> lock(mCacheMutex);

    M6CachedPagePtr cp = mLRUHead;

    while (cp != nullptr and cp->mPageNr != inPageNr)
        cp = cp->mNext;

    if (cp == nullptr or cp->mPage == nullptr)
    {
        M6IndexPageData* data = new M6IndexPageData;
        mFile.PRead(data, kM6IndexPageSize, inPageNr * kM6IndexPageSize);

        M6BasicPage* page;
        switch (data->leaf.mType)
        {
            case eM6IndexEmptyPage:            THROW(("Empty page!")); break;
            case eM6IndexBranchPage:        page = CreateBranchPage(data, inPageNr); break;
            case eM6IndexSimpleLeafPage:
            case eM6IndexMultiLeafPage:
            case eM6IndexMultiIDLLeafPage:    page = CreateLeafPage(data, inPageNr); break;
            case eM6IndexBitVectorPage:        page = new M6IndexBitVectorPage(*this, data, inPageNr); break;
            default:                        THROW(("Invalid index type in load (%c/%x)", data->leaf.mType, data->leaf.mType));
        }

        cp = GetCachePage();
        cp->mPage = page;
        cp->mPageNr = inPageNr;
        cp->mRefCount = 0;
    }

    cp->mRefCount += 1;

    Page* result = dynamic_cast<Page*>(cp->mPage);
    if (result == nullptr)
        THROW(("Error loading cache page"));
    return result;
}

template<class Page>
void M6IndexImpl::Release(Page*& ioPage)
{
    boost::unique_lock<boost::mutex> lock(mCacheMutex);

    assert(ioPage != nullptr);

    M6CachedPagePtr cp = mLRUHead;
    while (cp != nullptr and cp->mPage != ioPage)
        cp = cp->mNext;

    if (cp == nullptr)
        THROW(("Invalid page in Release"));

    cp->mRefCount -= 1;

    if (cp->mRefCount == 0 and ioPage->GetKind() == eM6IndexBitVectorPage)
    {
        if (ioPage->IsDirty())
            ioPage->Flush(mFile);

        delete ioPage;
        cp->mPage = nullptr;
        cp->mPageNr = 0;
    }

    ioPage = nullptr;
}

template<class Page>
void M6IndexImpl::Reference(Page* inPage)
{
    boost::unique_lock<boost::mutex> lock(mCacheMutex);

    assert(inPage != nullptr);

    M6CachedPagePtr cp = mLRUHead;
    while (cp != nullptr and cp->mPage != inPage)
        cp = cp->mNext;

    if (cp == nullptr)
        THROW(("Invalid page in Release"));

    cp->mRefCount += 1;
}

void M6IndexImpl::SwapPages(uint32 inPageA, uint32 inPageB)
{
    M6BasicPage* pageA = Load<M6BasicPage>(inPageA);
    M6BasicPage* pageB = Load<M6BasicPage>(inPageB);

    M6CachedPagePtr cpb = nullptr, cpa = nullptr;
    for (M6CachedPagePtr cp = mLRUHead; cp != nullptr and (cpa == nullptr or cpb == nullptr); cp = cp->mNext)
    {
        if (cp->mPage == pageA)
            cpa = cp;
        if (cp->mPage == pageB)
            cpb = cp;
    }

    assert(cpa->mPage == pageA);
    assert(cpb->mPage == pageB);

    swap(cpa->mPageNr, cpb->mPageNr);

    pageA->SetPageNr(inPageB);
    pageB->SetPageNr(inPageA);

    --cpa->mRefCount;
    --cpb->mRefCount;
}

void M6IndexImpl::SetAutoCommit(bool inAutoCommit)
{
    mAutoCommit = inAutoCommit;
    if (mAutoCommit == true)
    {
        Commit();
        mFile.PWrite(mHeader, 0);
    }
}

// --------------------------------------------------------------------

template<class M6DataType>
M6IndexImplT<M6DataType>::M6IndexImplT(M6BasicIndex& inIndex, const fs::path& inPath,
        M6IndexType inType, MOpenMode inMode)
    : M6IndexImpl(inIndex, inPath, inType, inMode)
    , mBatch(nullptr)
    , mBatchCount(0)
{
    InitCache(kM6CacheCount);
}

template<>
void M6IndexImplT<uint32>::Remap(uint32& ioData, const vector<uint32>& inRemappedBitPageNrs)
{
}

template<>
void M6IndexImplT<M6MultiData>::Remap(M6MultiData& ioData, const vector<uint32>& inRemappedBitPageNrs)
{
    if (ioData.mBitVector.mType == eM6OnDiskBitVector)
        ioData.mBitVector.mPage = inRemappedBitPageNrs[ioData.mBitVector.mPage];
}

template<>
void M6IndexImplT<M6MultiIDLData>::Remap(M6MultiIDLData& ioData, const vector<uint32>& inRemappedBitPageNrs)
{
    if (ioData.mBitVector.mType == eM6OnDiskBitVector)
        ioData.mBitVector.mPage = inRemappedBitPageNrs[ioData.mBitVector.mPage];
}

template<class M6DataType>
M6IndexImplT<M6DataType>::~M6IndexImplT()
{
    delete [] mBatch;
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
            delete mCache[ix].mPage;
            mCache[ix].mPage = nullptr;
            mCache[ix].mPageNr = 0;
            mCache[ix].mRefCount = 0;
        }
    }
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::GetKey(uint32 inPage, uint32 inKeyNr, string& outKey)
{
    LeafPage* page = Load<LeafPage>(inPage);
    if (inKeyNr >= page->GetN())
        THROW(("key index out of range"));
    outKey = page->GetKey(inKeyNr);
    Release(page);
}

template<class M6DataType>
bool M6IndexImplT<M6DataType>::GetNextKey(uint32& ioPage, uint32& ioKeyNr, string& outKey)
{
    bool result = false;

    LeafPage* page = Load<LeafPage>(ioPage);

    if (ioKeyNr + 1 < page->GetN())
    {
        ++ioKeyNr;
        GetKey(ioPage, ioKeyNr, outKey);
        result = true;
    }
    else
    {
        ioPage = page->GetLink();
        ioKeyNr = 0;
        if (ioPage != 0)
        {
            result = true;
            GetKey(ioPage, ioKeyNr, outKey);
        }
    }

    Release(page);
    return result;
}

template<>
uint32 M6IndexImplT<uint32>::GetCount(uint32 inPage, uint32 inKeyNr)
{
    return 1;
}

template<>
uint32 M6IndexImplT<M6MultiData>::GetCount(uint32 inPage, uint32 inKeyNr)
{
    uint32 result = 0;
    LeafPage* page = Load<LeafPage>(inPage);

    if (inKeyNr < page->GetN())
        result = page->GetValue(inKeyNr).mCount;

    Release(page);
    return result;
}

template<>
uint32 M6IndexImplT<M6MultiIDLData>::GetCount(uint32 inPage, uint32 inKeyNr)
{
    uint32 result = 0;
    LeafPage* page = Load<LeafPage>(inPage);

    if (inKeyNr < page->GetN())
        result = page->GetValue(inKeyNr).mCount;

    Release(page);
    return result;
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Insert(const string& inKey, const M6DataType& inValue)
{
    try
    {
        if (mHeader.mRoot == 0)    // empty index?
        {
            IndexPage* root(Allocate<LeafPage>());
            mHeader.mRoot = mHeader.mFirstLeafPage = root->GetPageNr();
            mHeader.mDepth = 1;
            Release(root);
        }

        IndexPage* root(Load<IndexPage>(mHeader.mRoot));

        string key(inKey);
        uint32 link;

        if (root->Insert(key, inValue, link))
        {
            // increase depth
            ++mHeader.mDepth;

            BranchPage* newRoot(Allocate<BranchPage>());
            newRoot->SetLink(mHeader.mRoot);
            newRoot->InsertKeyValue(key, link, 0);
            mHeader.mRoot = newRoot->GetPageNr();
            Release(newRoot);
        }

        Release(root);

// check for refcounted pages
#if DEBUG
for (uint32 ix = 0; ix < mCacheCount; ++ix)
    assert(mCache[ix].mRefCount == 0);
#endif

        ++mHeader.mSize;
        mDirty = true;

        if (mAutoCommit)
        {
            Commit();
            mFile.PWrite(mHeader, 0);
        }
    }
    catch (...)
    {
        Rollback();
        throw;
    }
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Insert(uint32 inKey, const M6DataType& inValue)
{
    if (mBatchFile == nullptr)
        THROW(("Insert called while not in batch mode"));

    ++mHeader.mSize;

    mBatch[mBatchCount].key = inKey;
    mBatch[mBatchCount].data = inValue;

    if (++mBatchCount >= kM6BatchSize)
        FlushBatch();
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::FlushBatch()
{
    auto compareKeys = [this](const char* sa, size_t la, const char* sb, size_t lb) -> int
        { return this->mIndex.CompareKeys(sa, la, sb, lb); };
    auto comparator = [=](const M6BatchEntry& a, const M6BatchEntry& b) -> bool
        { return this->mLexicon->Compare(a.key, b.key, compareKeys) < 0; };

    sort(mBatch, mBatch + mBatchCount, comparator);

    mBatchFile->Write(mBatch, sizeof(M6BatchEntry) * mBatchCount);
    mBatchCount = 0;
}

template<class M6DataType, class Comparator>
class M6BatchIterator
{
  public:
    typedef typename M6IndexImplT<M6DataType>::M6BatchEntry M6BatchEntry;
    struct M6BatchRunIterator;

    struct M6CompareBatchRun
    {
                    M6CompareBatchRun(M6Lexicon& inLexicon, Comparator inComparator)
                        : mLexicon(inLexicon), mCompare(inComparator) {}

        bool operator()(const M6BatchRunIterator& a, const M6BatchRunIterator& b)
            { return mLexicon.Compare(a.mValue.key, b.mValue.key, mCompare) > 0; }

        M6Lexicon&    mLexicon;
        Comparator    mCompare;
    };

                    M6BatchIterator(M6File& inFile, M6Lexicon& inLexicon, Comparator inComparator)
                        : mComparator(inLexicon, inComparator)
                    {
                        for (int64 offset = 0; offset < inFile.Size(); offset += kM6BatchSize * sizeof(M6BatchEntry))
                        {
                            M6BatchRunIterator r(&inFile, offset);
                            if (r.Next())
                                mQueue.push_back(r);
                        }
                        make_heap(mQueue.begin(), mQueue.end(), mComparator);
                    }

    bool            Next(string& outKey, M6DataType& outValue)
                    {
                        bool result = false;
                        if (not mQueue.empty())
                        {
                            pop_heap(mQueue.begin(), mQueue.end(), mComparator);
                            M6BatchRunIterator& r = mQueue.back();

                            result = true;
                            outKey = mComparator.mLexicon.GetString(r.mValue.key);
                            outValue = r.mValue.data;

                            if (r.Next())
                                push_heap(mQueue.begin(), mQueue.end(), mComparator);
                            else
                                mQueue.erase(mQueue.end() - 1);
                        }
                        return result;
                    }

    struct M6BatchRunIterator
    {
                    M6BatchRunIterator(M6File* inFile, int64 inOffset)
                        : mFile(inFile), mOffset(inOffset)
                    {
                        size_t n = (inFile->Size() - inOffset) / sizeof(M6BatchEntry);
                        if (n > kM6BatchSize)
                            n = kM6BatchSize;
                        mCount = static_cast<uint32>(n);
                    }

        bool        Next()
                    {
                        bool result = false;
                        if (mCount-- > 0)
                        {
                            mFile->PRead(mValue, mOffset);
                            mOffset += sizeof(mValue);
                            result = true;
                        }
                        return result;
                    }

        M6File*            mFile;
        int64            mOffset;
        M6BatchEntry    mValue;
        uint32            mCount;
    };

  private:

    M6CompareBatchRun    mComparator;
    vector<M6BatchRunIterator>
                        mQueue;
};

template<class M6DataType>
bool M6IndexImplT<M6DataType>::Erase(const string& inKey)
{
    if (mHeader.mRoot == 0)
        return false;

    bool result = false;

    try
    {
        IndexPage* root(Load<IndexPage>(mHeader.mRoot));

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

            if (mHeader.mRoot == 0)
                mHeader.mFirstLeafPage = 0;
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
        IndexPage* root(Load<IndexPage>(mHeader.mRoot));
        result = root->Find(inKey, outValue);
        Release(root);
    }
    return result;
}

template<class M6DataType>
M6Iterator* M6IndexImplT<M6DataType>::GetIterator(uint32 inPage, uint32 inKeyNr)
{
    LeafPage* page = Load<LeafPage>(inPage);
    M6Iterator* result = GetIterator(page->GetValue(inKeyNr));
    Release(page);
    return result;
}

template<class M6DataType>
M6Iterator* M6IndexImplT<M6DataType>::GetIterator(const M6DataType& inValue)
{
    return new M6MultiDocIterator(M6IBitStream(new M6IBitVectorImpl(*this, inValue.mBitVector)), inValue.mCount);
}

template<>
M6Iterator* M6IndexImplT<uint32>::GetIterator(const uint32& inValue)
{
    return new M6SingleDocIterator(inValue);
}

template<class M6DataType>
uint32 M6IndexImplT<M6DataType>::AddHits(const M6DataType& inValue, vector<bool>& outBitmap)
{
    uint32 updated;

    M6IBitStream bits(new M6IBitVectorImpl(*this, inValue.mBitVector));
    ReadSimpleArray(bits, inValue.mCount, outBitmap, updated);

    return updated;
}

template<>
uint32 M6IndexImplT<uint32>::AddHits(const uint32& inValue, vector<bool>& outBitmap)
{
    if (inValue < outBitmap.size())
        outBitmap[inValue] = true;

    return 1;
}

template<class M6DataType>
M6Iterator* M6IndexImplT<M6DataType>::Find(const string& inKey)
{
    M6Iterator* result = nullptr;
    if (mHeader.mRoot != 0)
    {
        IndexPage* root(Load<IndexPage>(mHeader.mRoot));
        M6DataType value;
        if (root->Find(this->mIndex.StringToKey(inKey), value))
            result = GetIterator(value);
        Release(root);
    }
    return result;
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Find(const string& inQuery, M6QueryOperator inOperator,
    vector<bool>& outBitmap, uint32& outCount)
{
    if (mHeader.mRoot == 0)
        return;

    uint32 page = 0, key = 0;
    string query = this->mIndex.StringToKey(inQuery);

    if (inOperator != eM6LessThan and inOperator != eM6LessOrEqual)
    {
        IndexPage* root(Load<IndexPage>(mHeader.mRoot));
        root->LowerBound(query, page, key);
        Release(root);
    }

    Visit([&](const char* inKey, uint32 inKeyLen, const M6DataType& inData) -> bool
    {
        bool result = false;

        int d = this->mIndex.CompareKeys(inKey, inKeyLen, query.c_str(), query.length());

        switch (inOperator)
        {
            case eM6Equals:
                if (d == 0)
                    outCount += this->AddHits(inData, outBitmap);
                break;

            case eM6LessThan:
                if (d < 0)
                {
                    result = true;
                    outCount += this->AddHits(inData, outBitmap);
                }
                break;

            case eM6LessOrEqual:
                if (d <= 0)
                {
                    result = true;
                    outCount += this->AddHits(inData, outBitmap);
                }
                break;

            case eM6GreaterOrEqual:
                if (d >= 0)
                    outCount += this->AddHits(inData, outBitmap);
                result = true;
                break;

            case eM6GreaterThan:
                if (d > 0)
                    outCount += this->AddHits(inData, outBitmap);
                result = true;
                break;

            default:
                assert(false);
                break;
        }

        return result;
    }, page, key);
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Find(const string& inLowerBound, const string& inUpperBound, vector<bool>& outBitmap, uint32& outCount)
{
    if (mHeader.mRoot == 0)
        return;

    uint32 page = 0, key = 0;

    IndexPage* root(Load<IndexPage>(mHeader.mRoot));
    root->LowerBound(inLowerBound, page, key);
    Release(root);

    Visit([&](const char* inKey, uint32 inKeyLen, const M6DataType& inData) -> bool
    {
        bool result = false;

        if (this->mIndex.CompareKeys(inKey, inKeyLen, inUpperBound.c_str(), inUpperBound.length()) <= 0)
        {
            outCount += this->AddHits(inData, outBitmap);
            result = true;
        }

        return result;
    }, page, key);
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::FindPattern(const string& inPattern, vector<bool>& outBitmap, uint32& outCount)
{
    if (mHeader.mRoot == 0)
        return;

    uint32 page = 0, key = 0;

    // pattern is a glob pattern
    string s(inPattern.substr(0, inPattern.find('*')));
    s = s.substr(0, s.find('?'));

    if (not s.empty())
    {
        IndexPage* root(Load<IndexPage>(mHeader.mRoot));
        root->LowerBound(s, page, key);
        Release(root);
    }

    Visit([&](const char* inKey, uint32 inKeyLen, const M6DataType& inData) -> bool
    {
        M6MatchResult r = M6Match(inPattern.c_str(), inKey, inKeyLen);

        if (r == eM6Match)
            outCount += this->AddHits(inData, outBitmap);

        return r != eM6NoMatchAndGreater;
    }, page, key);
}

template<class M6DataType>
M6Iterator* M6IndexImplT<M6DataType>::FindString(const string& inString)
{
    return nullptr;
}

template<>
M6Iterator* M6IndexImplT<M6MultiIDLData>::FindString(const string& inString)
{
    M6Iterator* result = nullptr;
    if (mHeader.mRoot != 0)
    {
        IndexPage* root(Load<IndexPage>(mHeader.mRoot));
        M6MultiIDLData data;
        vector<tuple<M6Iterator*,int64,uint32>> iterators;
        bool ok = true;
        uint32 index = 0;

        M6Tokenizer tokenizer(inString);
        for (;;)
        {
            M6Token token = tokenizer.GetNextWord();
            if (token == eM6TokenEOF)
                break;

            if (token == eM6TokenWord or token == eM6TokenNumber or token == eM6TokenFloat)
            {
                if (not root->Find(tokenizer.GetTokenString(), data))
                {
                    ok = false;
                    break;
                }

                iterators.push_back(make_tuple(
                    new M6MultiDocIterator(M6IBitStream(new M6IBitVectorImpl(*this, data.mBitVector)),
                            data.mCount), data.mIDLOffset, index));
                ++index;
            }
            else if (token == eM6TokenPunctuation)
                ++index;
        }

        Release(root);

        if (not ok)
        {
            for (auto i : iterators)
                delete get<0>(i);
            iterators.clear();
        }

        fs::path idlFile = mPath.parent_path() / (mPath.stem().string() + ".idl");
        result = new M6PhraseIterator(idlFile, iterators);
    }
    return result;
}

template<class M6DataType>
bool M6IndexImplT<M6DataType>::Contains(const string& inKey)
{
    bool result = false;
    if (mHeader.mRoot != 0)
    {
        IndexPage* root(Load<IndexPage>(mHeader.mRoot));
        M6DataType value;
        result = root->Find(inKey, value);
        Release(root);
    }
    return result;
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Vacuum(M6Progress& inProgress)
{
    int64 fileSize = mFile.Size();

    // keep an indirect array of reordered pages
    int64 pageCount = (fileSize / kM6IndexPageSize) + 1;
    vector<uint32> ix1(pageCount);
    iota(ix1.begin(), ix1.end(), 0);
    vector<uint32> ix2(ix1);

    //Get the address of the mapped region
    uint32 n = 1;    // page counter

    uint32 m = 0;

    // start by reordering the bit pages
    uint32 pageNr = mHeader.mFirstBitsPage;
    while (pageNr != 0)
    {
        pageNr = ix1[pageNr];
        if (pageNr != n)
        {
            swap(ix1[ix2[pageNr]], ix1[ix2[n]]);
            swap(ix2[pageNr], ix2[n]);
            SwapPages(pageNr, n);
            pageNr = n;
        }

        M6IndexBitVectorPage* page = Load<M6IndexBitVectorPage>(pageNr);

        pageNr = page->GetLink();
        if (pageNr != 0)
            page->SetLink(n + 1);
        ++n;
        Release(page);
    }

    if (mHeader.mFirstBitsPage)
        mHeader.mFirstBitsPage = 1;
    mHeader.mLastBitsPage = n - 1;

    // Now update the leaf pages
    deque<pair<string,uint32>> up;
    pageNr = mHeader.mFirstLeafPage;
    mHeader.mFirstLeafPage = n;

    while (pageNr != 0)
    {
        pageNr = ix1[pageNr];

        if (pageNr != n)
        {
            swap(ix1[ix2[pageNr]], ix1[ix2[n]]);
            swap(ix2[pageNr], ix2[n]);
            SwapPages(pageNr, n);
            pageNr = n;
        }

        LeafPage* page = Load<LeafPage>(pageNr);

        up.push_back(make_pair(page->GetKey(0), pageNr));
        uint32 link = page->GetLink();

        while (link != 0)
        {
            LeafPage* next = Load<LeafPage>(ix1[link]);
            if (next->GetN() == 0)
            {
                link = next->GetLink();
                Release(next);
                continue;
            }

            string key = next->GetKey(0);
//            assert(key.compare(page->GetKey(page->GetN() - 1)) > 0);
            if (not page->CanStore(key))
            {
                Release(next);
                break;
            }

            page->InsertKeyValue(key, next->GetValue(0), page->GetN());
            next->EraseEntry(0);
            Release(next);
        }

        // adjust bit vector indices in data
        for (uint32 i = 0; i < page->GetN(); ++i)
        {
            M6DataType value = page->GetValue(i);
            Remap(value, ix1);
            page->SetValue(i, value);
        }

        ++n;
        pageNr = link;

        if (link == 0)
            page->SetLink(0);
        else
            page->SetLink(n);

        m += page->GetN();
        inProgress.Consumed(page->GetN());

        Release(page);
    }

    assert(m == mHeader.mSize);

    FlushCache();
    mFile.Truncate(n * kM6IndexPageSize);
    CreateUpLevels(up);
    Commit();
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::SetBatchMode(M6Lexicon& inLexicon)
{
    if (mBatchFile != nullptr)
        THROW(("Already in batch mode!"));

    if (mHeader.mRoot != 0)    // not an empty index?
        THROW(("Batch mode request for a non-empty index is not supported yet... sorry"));

    mBatchFile = new M6File(mPath.parent_path() / (mPath.filename().string() + ".bf"), eReadWrite);
    mBatch = new M6BatchEntry[kM6BatchSize];
    mLexicon = &inLexicon;
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::FinishBatchMode(M6Progress& inProgress)
{
    if (mBatchFile == nullptr)
        THROW(("Not in batch mode"));

    // So we have to write out the batched entries

    if (mBatchCount > 0)
        FlushBatch();

    auto comparator = [this](const char* sa, size_t la, const char* sb, size_t lb) -> int
        { return this->mIndex.CompareKeys(sa, la, sb, lb); };

    M6BatchIterator<M6DataType,decltype(comparator)> iter(*mBatchFile, *mLexicon, comparator);

    string key;
    M6DataType v;

    if (not iter.Next(key, v))
        return;    // empty index

    uint32 n = 1;

    // Now create the leaf pages
    deque<pair<string,uint32>> up;

    // create a root
    LeafPage* page(Allocate<LeafPage>());
    mHeader.mRoot = mHeader.mFirstLeafPage = page->GetPageNr();
    mHeader.mDepth = 1;

    page->InsertKeyValue(key, v, page->GetN());
    up.push_back(make_pair(key, page->GetPageNr()));

    while (iter.Next(key, v))
    {
        ++n;

        if (not page->CanStore(key))
        {
            LeafPage* next = Allocate<LeafPage>();

            page->SetLink(next->GetPageNr());
            inProgress.Consumed(page->GetN());
            Release(page);
            page = next;

            up.push_back(make_pair(key, page->GetPageNr()));
        }

        page->InsertKeyValue(key, v, page->GetN());
    }

    assert(n == mHeader.mSize);

    inProgress.Consumed(page->GetN());
    Release(page);

    FlushCache();
    CreateUpLevels(up);
    Commit();

    delete mBatchFile;
    mBatchFile = nullptr;

    fs::remove(mPath.parent_path() / (mPath.filename().string() + ".bf"));
}

template<class M6DataType>
M6BasicPage* M6IndexImplT<M6DataType>::GetFirstLeafPage()
{
    M6BasicPage* result = nullptr;
    if (mHeader.mFirstLeafPage != 0)
        result = Load<LeafPage>(mHeader.mFirstLeafPage);
    return result;
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::CreateUpLevels(deque<pair<string,uint32>>& up)
{
    mHeader.mDepth = 1;
    while (up.size() > 1)
    {
        ++mHeader.mDepth;

        deque<pair<string,uint32>> nextUp;

        // we have at least two tuples, so take the first and use it as
        // link, and the second as first entry for the first page
        auto tuple = up.front();
        up.pop_front();

        BranchPage* page(Allocate<BranchPage>());
        page->SetLink(tuple.second);

        // store this new page for the next round
        nextUp.push_back(make_pair(tuple.first, page->GetPageNr()));

        while (not up.empty())
        {
            tuple = up.front();

            // make sure we never end up with an empty page
            if (page->CanStore(tuple.first))
            {
                if (up.size() == 1)
                {
                    page->InsertKeyValue(tuple.first, tuple.second, page->GetN());
                    up.pop_front();
                    break;
                }

                // special case, if up.size() == 2 and we can store both
                // keys, store them and break the loop
                if (up.size() == 2 and
                    page->GetN() + 1 < BranchPage::kM6EntryCount and
                    page->Free() >= (up[0].first.length() + up[1].first.length() + 2 + 2 * sizeof(uint32)))
                {
                    page->InsertKeyValue(up[0].first, up[0].second, page->GetN());
                    page->InsertKeyValue(up[1].first, up[1].second, page->GetN());
                    break;
                }

                // otherwise, only store the key if there's enough
                // in up to avoid an empty page
                if (up.size() > 2)
                {
                    page->InsertKeyValue(tuple.first, tuple.second, page->GetN());
                    up.pop_front();
                    continue;
                }
            }

            // cannot store the tuple, create new page
            Release(page);
            page = Allocate<BranchPage>();
            page->SetLink(tuple.second);

            nextUp.push_back(make_pair(tuple.first, page->GetPageNr()));
            up.pop_front();
        }

        Release(page);
        up = nextUp;
    }

    assert(up.size() == 1);
    mHeader.mRoot = up.front().second;
    mDirty = true;
}

template<class M6DataType>
M6BasicPage* M6IndexImplT<M6DataType>::CreateLeafPage(M6IndexPageData* inData, uint32 inPageNr)
{
    if (inData->leaf.mType != LeafPage::M6DataPageType::kIndexPageType)
        THROW(("Inconsistent page type, expected %c, found %c", LeafPage::M6DataPageType::kIndexPageType, inData->leaf.mType));
    return new LeafPage(*this, inData, inPageNr);
}

template<class M6DataType>
M6BasicPage* M6IndexImplT<M6DataType>::CreateBranchPage(M6IndexPageData* inData, uint32 inPageNr)
{
    if (inData->leaf.mType != BranchPage::M6DataPageType::kIndexPageType)
        THROW(("Inconsistent page type, expected %c, found %c", BranchPage::M6DataPageType::kIndexPageType, inData->leaf.mType));
    return new BranchPage(*this, inData, inPageNr);
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Visit(Visitor inVisitor, uint32 inPage, uint32 inKey)
{
    LeafPage* page;
    if (inPage != 0)
        page = Load<LeafPage>(inPage);
    else
        page = static_cast<LeafPage*>(GetFirstLeafPage());

    uint32 keyNr = inKey;
    while (page != nullptr)
    {
        const char* key;
        uint32 keyLen;
        const M6DataType* data;

        if (keyNr == page->GetN())
        {
            if (page->GetLink() == 0)
                break;

            LeafPage* next = Load<LeafPage>(page->GetLink());
            Release(page);
            page = next;
            keyNr = 0;
            continue;
        }

        tie(key, keyLen, data) = page->Peek(keyNr);
        ++keyNr;

        if (not inVisitor(key, keyLen, *data))
            break;
    }

    if (page != nullptr)
        Release(page);
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::GetBrowseSections(const string& inFirst, const string& inLast,
    uint32 inNrOfSections, vector<pair<string,string>>& outSections)
{
    outSections.clear();

    if (inNrOfSections == 0)
        return;

    IndexPage* root(Load<IndexPage>(mHeader.mRoot));

    LeafPage* beginPage;
    LeafPage* endPage;

    uint32 beginKey = 0, endKey = 0;

    if (inFirst.empty())
        beginPage = static_cast<LeafPage*>(GetFirstLeafPage());
    else
    {
        uint32 pageNr;
        root->LowerBound(inFirst, pageNr, beginKey);
        beginPage = Load<LeafPage>(pageNr);
    }

    if (inLast.empty())
        endPage = nullptr;
    else
    {
        uint32 pageNr;
        root->LowerBound(inLast, pageNr, endKey);
        endPage = Load<LeafPage>(pageNr);
    }

    Release(root);

    // We now have begin and end, unfortunately we now have to count
    // first to know the interval.

    uint32 n;

    if (inFirst.empty() and inLast.empty())
        n = mHeader.mSize;
    else if (beginPage == endPage)
        n = endKey - beginKey;
    else
    {
        n = beginPage->GetN() - beginKey;
        LeafPage* page = beginPage;
        Reference(page);

        for (;;)
        {
            if (page->GetLink() == 0)
            {
                Release(page);
                break;
            }

            LeafPage* next = Load<LeafPage>(page->GetLink());
            Release(page);
            page = next;

            if (page == endPage)
            {
                n += endKey;
                break;
            }

            n += page->GetN();
        }
    }

    // no use trying, we don't have enough keys to continue
    if (n < 2 * inNrOfSections)
        return;

    uint32 interval = n / inNrOfSections;

    // Now walk the same pages, picking first and last keys for each section

    LeafPage* page = beginPage;
    Reference(page);
    uint32 skip = interval;
    pair<string,string> section;
    section.first = this->mIndex.KeyToString(page->GetKey(beginKey));
    skip += beginKey;

    while (outSections.size() < inNrOfSections)
    {
        if (page->GetN() >= skip)
        {
            section.second = this->mIndex.KeyToString(page->GetKey(skip - 1));
            outSections.push_back(section);

            if (page->GetN() > skip)
            {
                section.first = this->mIndex.KeyToString(page->GetKey(skip));
                skip += interval;
                continue;
            }
        }

        if (page->GetLink() == 0 or page == endPage)
        {
            // last page, now if inLast is empty, take the last key of this page
            outSections.back().second = this->mIndex.KeyToString(page->GetKey(page->GetN() - 1));
            Release(page);
            break;
        }

        skip -= page->GetN();

        LeafPage* next = Load<LeafPage>(page->GetLink());
        Release(page);
        page = next;

        if (skip == 0)
        {
            section.first = this->mIndex.KeyToString(page->GetKey(0));
            skip += interval;
        }
    }

    // hmmmm, somehow sections miss the last key in the index, sometimes
    if (inLast.empty())
    {
        while (page->GetLink() != 0)
        {
            LeafPage* next = Load<LeafPage>(page->GetLink());
            Release(page);
            page = next;
        }

        outSections.back().second = this->mIndex.KeyToString(page->GetKey(page->GetN() - 1));
    }

    if (inLast.empty() == false and outSections.back().second != inLast)
        outSections.back().second = inLast;

    if (beginPage != nullptr)
        Release(beginPage);

    if (endPage != nullptr)
        Release(endPage);
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::GetBrowseEntries(
    const string& inFirst, const string& inLast, vector<string>& outEntries)
{
    outEntries.clear();

    uint32 page = 0, key = 0, n = 0;

    IndexPage* root(Load<IndexPage>(mHeader.mRoot));
    if (not inFirst.empty())
        root->LowerBound(this->mIndex.StringToKey(inFirst), page, key);
    Release(root);

    Visit([&](const char* inKey, uint32 inKeyLen, const M6DataType& inData) -> bool
    {
        string key = this->mIndex.KeyToString(string(inKey, inKey + inKeyLen));

        outEntries.push_back(key);

        return ++n < 100 and (inLast.empty() or CompareKeys(inLast, key) > 0);
    }, page, key);
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::VisitKeys(M6BasicIndex::KeyVisitor inVisitor)
{
    LeafPage* page = static_cast<LeafPage*>(GetFirstLeafPage());

    uint32 keyNr = 0;
    while (page != nullptr)
    {
        const char* key;
        uint32 keyLen;
        const M6DataType* data;

        if (keyNr == page->GetN())
        {
            if (page->GetLink() == 0)
                break;

            LeafPage* next = Load<LeafPage>(page->GetLink());
            Release(page);
            page = next;
            keyNr = 0;
            continue;
        }

        tie(key, keyLen, data) = page->Peek(keyNr);
        ++keyNr;

        if (not inVisitor(key, keyLen, M6CountData(*data)))
            break;
    }

    if (page != nullptr)
        Release(page);
}

template<class M6DataType>
void M6IndexImplT<M6DataType>::Validate()
{
    try
    {
        if (mHeader.mRoot != 0)
        {
            IndexPage* root(Load<IndexPage>(mHeader.mRoot));
            root->Validate("", nullptr);
            Release(root);
        }
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

    IndexPage* root(Load<IndexPage>(mHeader.mRoot));
    root->Dump(0, nullptr);
    Release(root);
}

// --------------------------------------------------------------------

M6BasicIndex::iterator M6IndexImpl::Begin()
{
    return
        mHeader.mSize > 0 ?
            M6BasicIndex::iterator(this, mHeader.mFirstLeafPage, 0) :
            M6BasicIndex::iterator(nullptr, 0, 0);
}

M6BasicIndex::iterator M6IndexImpl::End()
{
    return M6BasicIndex::iterator(nullptr, 0, 0);
}

M6BasicIndex::iterator M6IndexImpl::At(uint32 inPage, uint32 inKeyNr)
{
    return M6BasicIndex::iterator(inPage == 0 and inKeyNr == 0 ? nullptr : this, inPage, inKeyNr);
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
    if (mIndex != nullptr and mPage != 0)
        mIndex->GetKey(inPageNr, inKeyNr, mCurrent);
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
    if (not mIndex->GetNextKey(mPage, mKeyNr, mCurrent))
    {
        mIndex = nullptr;
        mPage = 0;
        mKeyNr = 0;
        mCurrent.clear();
    }

    return *this;
}

M6Iterator* M6BasicIndex::iterator::GetDocuments() const
{
    return mIndex->GetIterator(mPage, mKeyNr);
}

uint32 M6BasicIndex::iterator::GetCount() const
{
    return mIndex->GetCount(mPage, mKeyNr);
}

// --------------------------------------------------------------------

M6BasicIndex::M6BasicIndex(const fs::path& inPath, M6IndexType inIndexType, MOpenMode inMode)
    : mImpl(new M6IndexImplT<uint32>(*this, inPath, inIndexType, inMode))
{
}

M6BasicIndex::M6BasicIndex(M6IndexImpl* inImpl)
    : mImpl(inImpl)
{
}

M6BasicIndex::~M6BasicIndex()
{
    delete mImpl;
}

M6IndexType M6BasicIndex::GetIndexType() const
{
    return mImpl->GetIndexType();
}

void M6BasicIndex::Vacuum(M6Progress& inProgress)
{
    mImpl->Vacuum(inProgress);
}

M6BasicIndex::iterator M6BasicIndex::begin() const
{
    if (size() == 0)
        return mImpl->End();
    else
        return mImpl->Begin();
}

M6BasicIndex::iterator M6BasicIndex::end() const
{
    return mImpl->End();
}

void M6BasicIndex::GetBrowseSections(const string& inFirst, const string& inLast,
    uint32 inNrOfSections, vector<pair<string,string>>& outSections)
{
    mImpl->GetBrowseSections(inFirst, inLast, inNrOfSections, outSections);
}

void M6BasicIndex::GetBrowseEntries(const string& inFirst, const string& inLast,
    vector<string>& outEntries)
{
    mImpl->GetBrowseEntries(inFirst, inLast, outEntries);
}

void M6BasicIndex::VisitKeys(KeyVisitor inVisitor)
{
    mImpl->VisitKeys(inVisitor);
}

void M6BasicIndex::Insert(const string& key, uint32 value)
{
    if (key.length() >= kM6MaxKeyLength)
        THROW(("Invalid key length"));

    mImpl->Insert(StringToKey(key), value);
}

void M6BasicIndex::Insert(uint32 key, uint32 value)
{
    mImpl->Insert(key, value);
}

void M6BasicIndex::Erase(const string& key)
{
    mImpl->Erase(key);
}

bool M6BasicIndex::Find(const string& inKey, uint32& outValue)
{
    return mImpl->Find(inKey, outValue);
}

M6Iterator* M6BasicIndex::Find(const string& inKey)
{
    return mImpl->Find(inKey);
}

void M6BasicIndex::Find(const string& inKey, M6QueryOperator inOperator,
    vector<bool>& outBitmap, uint32& outCount)
{
    mImpl->Find(inKey, inOperator, outBitmap, outCount);
}

void M6BasicIndex::Find(const string& inLowerBound, const string& inUpperBound,
    vector<bool>& outBitmap, uint32& outCount)
{
    mImpl->Find(inLowerBound, inUpperBound, outBitmap, outCount);
}

void M6BasicIndex::FindPattern(const string& inPattern, vector<bool>& outBitmap, uint32& outCount)
{
    mImpl->FindPattern(inPattern, outBitmap, outCount);
}

M6Iterator* M6BasicIndex::FindString(const string& inString)
{
    return mImpl->FindString(inString);
}

bool M6BasicIndex::Contains(const string& inKey)
{
    return mImpl->Contains(inKey);
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

void M6BasicIndex::SetBatchMode(M6Lexicon& inLexicon)
{
    mImpl->SetBatchMode(inLexicon);
}

void M6BasicIndex::FinishBatchMode(M6Progress& inProgress, exception_ptr& outException)
{
    try
    {
        mImpl->FinishBatchMode(inProgress);
    }
    catch (exception& e)
    {
        cerr << "exception in thread: " << e.what() << endl;
        outException = current_exception();
    }
}

bool M6BasicIndex::IsInBatchMode()
{
    return mImpl->IsInBatchMode();
}

// --------------------------------------------------------------------

void M6BasicIndex::Dump() const
{
    mImpl->Dump();
}

void M6BasicIndex::Validate() const
{
    mImpl->Validate();
}

// --------------------------------------------------------------------

M6MultiBasicIndex::M6MultiBasicIndex(const fs::path& inPath, M6IndexType inIndexType, MOpenMode inMode)
    : M6BasicIndex(new M6IndexImplT<M6MultiData>(*this, inPath, inIndexType, inMode))
{
}

void M6MultiBasicIndex::Insert(const string& inKey, const vector<uint32>& inDocuments)
{
    M6MultiData data = { static_cast<uint32>(inDocuments.size()) };

    M6OBitStream bits;
    CompressSimpleArraySelector(bits, inDocuments);
    mImpl->StoreBits(bits, data.mBitVector);

    mImpl->Insert(StringToKey(inKey), data);
}

void M6MultiBasicIndex::Insert(uint32 inKey, const vector<uint32>& inDocuments)
{
    M6MultiData data = { static_cast<uint32>(inDocuments.size()) };

    M6OBitStream bits;
    CompressSimpleArraySelector(bits, inDocuments);
    mImpl->StoreBits(bits, data.mBitVector);

    mImpl->Insert(to_string(inKey), data);
}

void M6MultiBasicIndex::Insert(double inKey, const vector<uint32>& inDocuments)
{
   M6MultiData data = { static_cast<uint32>(inDocuments.size()) };

   M6OBitStream bits;
   CompressSimpleArraySelector(bits, inDocuments);
   mImpl->StoreBits(bits, data.mBitVector);

   mImpl->Insert(to_string(inKey), data);
}

//bool M6MultiBasicIndex::Find(const string& inKey, M6CompressedArray& outDocuments)
//{
//    bool result = false;
//    M6MultiData data;
//    if (mImpl->Find(inKey, data))
//    {
//        outDocuments = M6CompressedArray(M6IBitStream(new M6IBitVectorImpl(*mImpl, data.mBitVector)), data.mCount);
//
//        result = true;
//    }
//    return result;
//}

// --------------------------------------------------------------------

M6MultiIDLBasicIndex::M6MultiIDLBasicIndex(const fs::path& inPath, M6IndexType inIndexType, MOpenMode inMode)
    : M6BasicIndex(new M6IndexImplT<M6MultiIDLData>(*this, inPath, inIndexType, inMode))
{
}

void M6MultiIDLBasicIndex::Insert(uint32 inKey, int64 inIDLOffset, const vector<uint32>& inDocuments)
{
    M6MultiIDLData data = { static_cast<uint32>(inDocuments.size()) };
    data.mIDLOffset = inIDLOffset;

    M6OBitStream bits;
    CompressSimpleArraySelector(bits, inDocuments);
    mImpl->StoreBits(bits, data.mBitVector);

    mImpl->Insert(inKey, data);
}

//bool M6MultiIDLBasicIndex::Find(const string& inKey, M6CompressedArray& outDocuments, int64& outIDLOffset)
//{
//    return false;
//}

// --------------------------------------------------------------------

struct M6WeightedBasicIndexImpl : public M6IndexImplT<M6MultiData>
{
    typedef M6IndexImplT<M6MultiData> M6IndexImplBase;

                    M6WeightedBasicIndexImpl(M6BasicIndex& inIndex, const fs::path& inPath,
                            M6IndexType inType, MOpenMode inMode)
                        : M6IndexImplBase(inIndex, inPath, inType, inMode) {}

    virtual M6Iterator*
                    GetIterator(const M6MultiData& inValue);

    virtual uint32    AddHits(const M6MultiData& inValue, vector<bool>& outBitmap);
};

M6Iterator* M6WeightedBasicIndexImpl::GetIterator(const M6MultiData& inValue)
{
    vector<pair<uint32,float>> docs;
    docs.reserve(inValue.mCount);

    M6WeightedBasicIndex::M6WeightedIterator iter(*this, inValue.mBitVector, inValue.mCount, GetMaxWeight());

    uint32 docNr;
    uint8 weight;
    while (iter.Next(docNr, weight))
        docs.push_back(make_pair(docNr, 1.0f));
    assert(docs.size() == inValue.mCount);

    sort(docs.begin(), docs.end(), [](const pair<uint32,float>& a, const pair<uint32,float>& b) -> bool { return a.first < b.first; });

    return new M6VectorIterator(docs);
}

uint32 M6WeightedBasicIndexImpl::AddHits(const M6MultiData& inValue, vector<bool>& outBitmap)
{
    M6IBitStream bits(new M6IBitVectorImpl(*this, inValue.mBitVector));

    uint32 result = 0, total = inValue.mCount;

    while (total > 0)
    {
        uint32 delta, count, updated;
        ReadGamma(bits, delta);

        ReadArray(bits, outBitmap, count, updated);

        total -= count;
        result += updated;
    }

    return result;
}

M6WeightedBasicIndex::M6WeightedBasicIndex(const fs::path& inPath, M6IndexType inIndexType, MOpenMode inMode)
    : M6BasicIndex(new M6WeightedBasicIndexImpl(*this, inPath, inIndexType, inMode))
{
}

void M6WeightedBasicIndex::StoreDocumentBits(std::vector<std::pair<uint32,uint8>>& inDocuments, M6MultiData& data)
//void M6WeightedBasicIndex::Insert(uint32 inKey, vector<pair<uint32,uint8>>& inDocuments)
{
//    M6MultiData data = { static_cast<uint32>(inDocuments.size()) };
    if (mImpl->GetMaxWeight() == 0)
        THROW(("Max Weight wat not set, yet"));

    M6OBitStream bits;

    sort(inDocuments.begin(), inDocuments.end(),
        [](const pair<uint32,uint8>& a, const pair<uint32,uint8>& b) -> bool
            { return a.second > b.second or (a.second == b.second and a.first < b.first); }
    );

    std::vector<uint32> values;
    values.reserve(inDocuments.size());

    auto v = inDocuments.begin();

    uint32 lastWeight = mImpl->GetMaxWeight() + 1;
    while (v != inDocuments.end())
    {
        uint32 w = v->second;

        while (v != inDocuments.end() and v->second == w)
        {
            values.push_back(v->first);
            ++v;
        }

        if (values.size())
        {
            uint8 d = lastWeight - w;
            WriteGamma(bits, d);

            // use write array, since we need to know how many entries are in each run
            WriteArray(bits, values);

            values.clear();
            lastWeight = w;
        }
    }

    mImpl->StoreBits(bits, data.mBitVector);

//    mImpl->Insert(inKey, data);
}

void M6WeightedBasicIndex::Insert(uint32 inKey, vector<pair<uint32,uint8>>& inDocuments)
{
    M6MultiData data = { static_cast<uint32>(inDocuments.size()) };

    StoreDocumentBits(inDocuments, data);

    mImpl->Insert(inKey, data);
}

void M6WeightedBasicIndex::Insert(const string& inKey, vector<pair<uint32,uint8>>& inDocuments)
{
    M6MultiData data = { static_cast<uint32>(inDocuments.size()) };

    StoreDocumentBits(inDocuments, data);

    mImpl->Insert(inKey, data);
}

M6WeightedBasicIndex::M6WeightedIterator::M6WeightedIterator()
{
}

M6WeightedBasicIndex::M6WeightedIterator::M6WeightedIterator(M6IndexImpl& inIndex,
    const M6BitVector& inBitVector, uint32 inCount, uint32 inMaxWeight)
    : mBits(new M6IBitVectorImpl(inIndex, inBitVector))
    , mCount(inCount)
    , mWeight(inMaxWeight + 1)
{
}

M6WeightedBasicIndex::M6WeightedIterator::M6WeightedIterator(const M6WeightedIterator& inIter)
    : mBits(inIter.mBits)
    , mDocs(inIter.mDocs)
    , mCount(inIter.mCount)
    , mWeight(inIter.mWeight)
{
}

M6WeightedBasicIndex::M6WeightedIterator::M6WeightedIterator(M6WeightedIterator&& inIter)
    : mBits(move(inIter.mBits))
    , mDocs(move(inIter.mDocs))
    , mCount(inIter.mCount)
    , mWeight(inIter.mWeight)
{
}

M6WeightedBasicIndex::M6WeightedIterator&
M6WeightedBasicIndex::M6WeightedIterator::operator=(const M6WeightedIterator& inIter)
{
    if (this != &inIter)
    {
        mBits = inIter.mBits;
        mDocs = inIter.mDocs;
        mCount = inIter.mCount;
        mWeight = inIter.mWeight;
    }

    return *this;
}

M6WeightedBasicIndex::M6WeightedIterator&
M6WeightedBasicIndex::M6WeightedIterator::operator=(M6WeightedIterator&& inIter)
{
    if (this != &inIter)
    {
        mBits = move(inIter.mBits);
        mDocs = move(inIter.mDocs);
        mCount = inIter.mCount;
        mWeight = inIter.mWeight;
    }

    return *this;
}

bool M6WeightedBasicIndex::M6WeightedIterator::Next(uint32& outDocNr, uint8& outWeight)
{
    bool result = false;
    if (mCount > 0)
    {
        if (mDocs.empty())
        {
            uint32 delta;
            ReadGamma(mBits, delta);
            mWeight -= delta;

            ReadArray(mBits, mDocs);
            reverse(mDocs.begin(), mDocs.end());
        }

        outDocNr = mDocs.back();
        mDocs.pop_back();

        outWeight = mWeight;

        --mCount;
        result = true;
    }

    return result;
}

bool M6WeightedBasicIndex::Find(const string& inKey, M6WeightedIterator& outIterator)
{
    bool result = false;
    M6MultiData data;
    if (mImpl->Find(inKey, data))
    {
        outIterator = M6WeightedIterator(*mImpl, data.mBitVector, data.mCount, mImpl->GetMaxWeight());
        result = true;
    }
    return result;
}

void M6WeightedBasicIndex::CalculateDocumentWeights(uint32 inDocCount,
    vector<float>& outWeights, M6Progress& inProgress)
{
    typedef M6LeafPage<M6MultiData> LeafPage;

    vector<uint32> docs;
    float max = static_cast<float>(inDocCount);

    M6BasicPage* page = mImpl->GetFirstLeafPage();
    uint32 cntr = 0;

    while (page != nullptr)
    {
        LeafPage* leaf = dynamic_cast<LeafPage*>(page);
        if (leaf == nullptr)
            THROW(("invalid index"));

        for (uint32 i = 0; i < leaf->GetN(); ++i)
        {
            M6MultiData data = leaf->GetValue(i);
            M6IBitStream bits(new M6IBitVectorImpl(*mImpl, data.mBitVector));

            uint32 weight = mImpl->GetMaxWeight() + 1;

            uint32 count = data.mCount;
            float idfCorrection = log(1.f + max / count);

            while (count > 0)
            {
                uint32 delta;
                ReadGamma(bits, delta);
                weight -= delta;

                float docTermWeight = weight * idfCorrection;
                docTermWeight *= docTermWeight;

                ReadArray(bits, docs);

                for (uint32 doc : docs)
                    outWeights[doc] += docTermWeight;

                count -= static_cast<uint32>(docs.size());
            }

            ++cntr;
        }

        inProgress.Progress(cntr);

        uint32 link = page->GetLink();
        if (link == 0)
            break;

        M6BasicPage* next = mImpl->Load<M6BasicPage>(page->GetLink());
        mImpl->Release(page);
        page = next;
    }

    transform(outWeights.begin(), outWeights.end(), outWeights.begin(),
        [](float w) -> float { return sqrt(w); });
}

void M6WeightedBasicIndex::SetMaxWeight(uint32 inMaxWeight)
{
    mImpl->SetMaxWeight(inMaxWeight);
}

uint32 M6WeightedBasicIndex::GetMaxWeight() const
{
    return mImpl->GetMaxWeight();
}

// --------------------------------------------------------------------

M6BasicIndex* M6BasicIndex::Load(const fs::path& inFile)
{
    // first read the signature
    M6IxFileHeader header;
    {
        M6File file(inFile, eReadOnly);
        file.PRead(header, 0);
    }

    M6BasicIndex* index = nullptr;

    switch (header.mSignature)
    {
        case eM6CharIndex:            index = new M6SimpleIndex(inFile, eReadOnly);        break;
        case eM6NumberIndex:        index = new M6NumberIndex(inFile, eReadOnly);        break;
        case eM6FloatIndex:         index = new M6FloatIndex(inFile, eReadOnly);        break;
//        case eM6DateIndex:            index = new M6SimpleIndex(inFile, eReadOnly);        break;
        case eM6CharMultiIndex:        index = new M6SimpleMultiIndex(inFile, eReadOnly);    break;
        case eM6NumberMultiIndex:    index = new M6NumberMultiIndex(inFile, eReadOnly);     break;
        case eM6FloatMultiIndex:    index = new M6FloatMultiIndex(inFile, eReadOnly);   break;
//        case eM6DateMultiIndex:        index = new M6SimpleMultiIndex(inFile, eReadOnly);    break;
        case eM6CharMultiIDLIndex:    index = new M6SimpleIDLMultiIndex(inFile, eReadOnly); break;
        case eM6CharWeightedIndex:    index = new M6SimpleWeightedIndex(inFile, eReadOnly); break;
        default:                    THROW(("Unknown index type"));
    }

    return index;
}


template<>
void M6LeafPage<M6MultiData>::Dump(int inLevel, BranchPage* inParent)
{
    string prefix(inLevel * 2, ' ');

    cout << prefix << "leaf page at " << mPageNr << "; N = " << mData->mN << ": [" << endl;
    for (int i = 0; i < mData->mN; ++i)
    {
        const M6MultiData& data = GetValue(i);

        M6IBitStream bits(new M6IBitVectorImpl(mIndex, data.mBitVector));
        vector<uint32> docs;
        ReadArray(bits, docs);

//        assert(docs.size() == data.mCount);

        cout << prefix << "  " << GetKey(i) << '(' << data.mCount << ')'
             << (i + 1 < mData->mN ? ", " : "") << endl;

        cout << prefix << "   [";
        bool first = true;
        for (uint32 doc : docs)
        {
            if (not first) cout << ", ";
            first = false;
            cout << doc;
        }
        cout << ']' << endl;
    }
    cout << prefix << "]" << endl;

    if (mData->mLink)
    {
        LeafPage* next(mIndex.Load<LeafPage>(mData->mLink));
        cout << prefix << "  " << "link: " << next->GetKey(0) << endl;
        mIndex.Release(next);
    }
}

