#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Error.h"
#include "M6Document.h"
#include "M6Databank.h"
#include "M6DataSource.h"
#include "M6Progress.h"
#include "M6Iterator.h"

using namespace std;
namespace fs = boost::filesystem;
namespace io = boost::iostreams;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

namespace M6Blast {

template<int n> inline uint32 powi(uint32 x) { return powi<n - 1>(x) * x; }
template<> inline uint32 powi<1>(uint32 x) { return x; }

const uint32
	kM6AACount = 22,
	kM6Bits = 5,
	kM6Threshold = 11,
	kM6HitWindow = 40,
	kM6UnusedDiagonal = 0xf3f3f3f3;

// 22 real letters and 1 dummy
const char kM6Residues[] = "ABCDEFGHIKLMNPQRSTVWYZX";

inline int ResidueNr(char inAA)
{
	const static int8 kResidueNrTable[] = {
	//	A   B   C   D   E   F   G   H   I       K   L   M   N   P   Q   R   S   T  U=X  V   W   X   Y   Z
		0,  1,  2,  3,  4,  5,  6,  7,  8, -1,  9, 10, 11, 12, 13, 14, 15, 16, 17, 22, 18, 19, 22, 20, 21
	};
	
	inAA &= ~(040);
	int result = -1;
	if (inAA >= 'A' and inAA <= 'Z')
		result = kResidueNrTable[inAA - 'A'];
	return result;
}

const int8 kM6Blosum62[] = {
	  4,                                                                                                               // A
	 -2,   4,                                                                                                          // B
	  0,  -3,   9,                                                                                                     // C
	 -2,   4,  -3,   6,                                                                                                // D
	 -1,   1,  -4,   2,   5,                                                                                           // E
	 -2,  -3,  -2,  -3,  -3,   6,                                                                                      // F
	  0,  -1,  -3,  -1,  -2,  -3,   6,                                                                                 // G
	 -2,   0,  -3,  -1,   0,  -1,  -2,   8,                                                                            // H
	 -1,  -3,  -1,  -3,  -3,   0,  -4,  -3,   4,                                                                       // I
	 -1,   0,  -3,  -1,   1,  -3,  -2,  -1,  -3,   5,                                                                  // K
	 -1,  -4,  -1,  -4,  -3,   0,  -4,  -3,   2,  -2,   4,                                                             // L
	 -1,  -3,  -1,  -3,  -2,   0,  -3,  -2,   1,  -1,   2,   5,                                                        // M
	 -2,   3,  -3,   1,   0,  -3,   0,   1,  -3,   0,  -3,  -2,   6,                                                   // N
	 -1,  -2,  -3,  -1,  -1,  -4,  -2,  -2,  -3,  -1,  -3,  -2,  -2,   7,                                              // P
	 -1,   0,  -3,   0,   2,  -3,  -2,   0,  -3,   1,  -2,   0,   0,  -1,   5,                                         // Q
	 -1,  -1,  -3,  -2,   0,  -3,  -2,   0,  -3,   2,  -2,  -1,   0,  -2,   1,   5,                                    // R
	  1,   0,  -1,   0,   0,  -2,   0,  -1,  -2,   0,  -2,  -1,   1,  -1,   0,  -1,   4,                               // S
	  0,  -1,  -1,  -1,  -1,  -2,  -2,  -2,  -1,  -1,  -1,  -1,   0,  -1,  -1,  -1,   1,   5,                          // T
	  0,  -3,  -1,  -3,  -2,  -1,  -3,  -3,   3,  -2,   1,   1,  -3,  -2,  -2,  -3,  -2,   0,   4,                     // V
	 -3,  -4,  -2,  -4,  -3,   1,  -2,  -2,  -3,  -3,  -2,  -1,  -4,  -4,  -2,  -3,  -3,  -2,  -3,  11,                // W
	 -2,  -3,  -2,  -3,  -2,   3,  -3,   2,  -1,  -2,  -1,  -1,  -2,  -3,  -1,  -2,  -2,  -2,  -1,   2,   7,           // Y
	 -1,   1,  -3,   1,   4,  -3,  -2,   0,  -3,   1,  -3,  -1,   0,  -1,   3,   0,   0,  -1,  -2,  -3,  -2,   4,      // Z
	  0,  -1,  -2,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -2,  -1,  -1,   0,   0,  -1,  -2,  -1,  -1,  -1, // X
};

class M6Matrix
{
  public:
					M6Matrix(const int8* inData) : mData(inData) {}

	int8			operator()(char inAA1, char inAA2) const;

  private:
	const int8*		mData;
};

inline int8 M6Matrix::operator()(char inAA1, char inAA2) const
{
	int rn1 = ResidueNr(inAA1);
	int rn2 = ResidueNr(inAA2);
	
	int result = -4;
	
	if (rn1 >= 0 and rn2 >= 0)
	{
		if (rn1 > rn2)
			result = kM6Blosum62[(rn2 * (rn2 + 1)) / 2 + rn1];
		else
			result = kM6Blosum62[(rn1 * (rn1 + 1)) / 2 + rn2];
	}

	return result;	
}

const M6Matrix	kM6Blosum62Matrix(kM6Blosum62);

// --------------------------------------------------------------------

template<int WORDSIZE>
struct M6Word
{
	static const uint32 kM6MaxWordIndex, kM6MaxIndex;

					M6Word()
					{
						for (uint32 i = 0; i <= WORDSIZE; ++i)
							aa[i] = 0;
					}

					M6Word(const char* inSequence)
					{
						for (uint32 i = 0; i < WORDSIZE; ++i)
							aa[i] = inSequence[i];
						aa[WORDSIZE] = 0;
					}

	uint8&			operator[](uint32 ix)	{ return aa[ix]; }
	const char*		c_str() const			{ return reinterpret_cast<const char*>(aa); }
	size_t			length() const			{ return WORDSIZE; }

	class PermutationIterator
	{
	  public:
						PermutationIterator(M6Word inWord, const M6Matrix& inMatrix, int32 inThreshold)
							: mWord(inWord), mMatrix(inMatrix), mThreshold(inThreshold), mIndex(0) {}
		
		bool			Next(uint32& outIndex);
	
	  private:
		M6Word			mWord;
		uint32			mIndex;
		const M6Matrix&	mMatrix;
		int32			mThreshold;
	};

	uint8			aa[WORDSIZE + 1];
};

template<> const uint32 M6Word<3>::kM6MaxWordIndex = 0x07FFF;
template<> const uint32 M6Word<3>::kM6MaxIndex = kM6AACount * kM6AACount * kM6AACount;

template<int WORDSIZE>
bool M6Word<WORDSIZE>::PermutationIterator::Next(uint32& outIndex)
{
	bool result = false;
	M6Word w;

	while (mIndex < kM6MaxIndex)
	{
		uint32 ix = mIndex;
		++mIndex;

		int32 score = 0;
		outIndex = 0;
		
		for (uint32 i = 0; i < WORDSIZE; ++i)
		{
			uint32 resNr = ix % kM6AACount;
			w[i] = kM6Residues[resNr];
			ix /= kM6AACount;
			score += mMatrix(mWord[i], w[i]);
			outIndex = outIndex << kM6Bits | resNr;
		}
		
		if (score >= mThreshold)
		{
			result = true;
			break;
		}
	}
	
	return result;
}

template<int WORDSIZE>
class M6WordHitIterator
{
	enum { kInlineCount = 2 };

	static const uint32 kMask;
	
	struct Entry
	{
		uint16					mCount;
		uint16					mDataOffset;
		uint16					mInline[kInlineCount];
	};

  public:

	typedef M6Word<WORDSIZE>					Word;
	typedef typename Word::PermutationIterator	WordPermutationIterator;

	struct WordHitIteratorStaticData
	{
		vector<Entry>			mLookup;
		vector<uint32>			mOffsets;
	};
								M6WordHitIterator(WordHitIteratorStaticData& inStaticData)
									: mLookup(inStaticData.mLookup), mOffsets(inStaticData.mOffsets) {}
	
	static void					Init(const char* inQuery, size_t inQueryLength, const M6Matrix& inMatrix,
									uint32 inThreshhold, WordHitIteratorStaticData& outStaticData);

	void						Reset(const char* inSequenceBegin, const char* inSequenceEnd);
	bool						Next(uint16& outQueryOffset, uint16& outTargetOffset);
	uint32						Index() const		{ return mIndex; }

  private:

	const char*		mSeqBegin;
	const char*		mSeqCurrent;
	const char*		mSeqEnd;
	vector<Entry>&	mLookup;
	vector<uint32>&	mOffsets;
	uint32			mIndex;
	Entry			mCurrent;
};

template<> const uint32 M6WordHitIterator<3>::kMask = 0x03FF;

template<int WORDSIZE>
void M6WordHitIterator<WORDSIZE>::Init(const char* inQuery, size_t inQueryLength, 
	const M6Matrix& inMatrix, uint32 inThreshhold, WordHitIteratorStaticData& outStaticData)
{
	uint64 N = Word::kM6MaxWordIndex;
	
	vector<vector<uint32> > test(N);
	
	for (uint32 i = 0; i < inQueryLength - WORDSIZE + 1; ++i)
	{
		Word w(inQuery++);
		
		WordPermutationIterator p(w, inMatrix, inThreshhold);
		uint32 ix;

		while (p.Next(ix))
			test[ix].push_back(i);
	}
	
	size_t M = 0;
	for (uint32 i = 0; i < N; ++i)
		M += test[i].size();
	
	outStaticData.mLookup = vector<Entry>(N);
	outStaticData.mOffsets = vector<uint32>(M);

	uint32* data = &outStaticData.mOffsets[0];

	for (uint32 i = 0; i < N; ++i)
	{
		outStaticData.mLookup[i].mCount = static_cast<uint16>(test[i].size());
		outStaticData.mLookup[i].mDataOffset = static_cast<uint16>(data - &outStaticData.mOffsets[0]);

		for (uint32 j = 0; j < outStaticData.mLookup[i].mCount; ++j)
		{
			if (j >= kInlineCount)
				*data++ = test[i][j];
			else
				outStaticData.mLookup[i].mInline[j] = test[i][j];
		}
	}
	
	assert(data < &outStaticData.mOffsets[0] + M);
}

template<int WORDSIZE>
void M6WordHitIterator<WORDSIZE>::Reset(const char* inSequenceBegin, const char* inSequenceEnd)
{
	mSeqBegin = mSeqCurrent = inSequenceBegin;
	mSeqEnd = inSequenceEnd;
	
	mIndex = 0;
	mCurrent.mCount = 0;

	for (uint32 i = 0; i < WORDSIZE - 1 and mSeqCurrent != mSeqEnd; ++i)
		mIndex = mIndex << kM6Bits | ResidueNr(*mSeqCurrent++);
}

template<int WORDSIZE>
bool M6WordHitIterator<WORDSIZE>::Next(uint16& outQueryOffset, uint16& outTargetOffset)
{
	bool result = false;
	
	for (;;)
	{
		if (mCurrent.mCount-- > 0)
		{
			if (mCurrent.mCount >= kInlineCount)
				outQueryOffset = mOffsets[mCurrent.mDataOffset + mCurrent.mCount - kInlineCount];
			else
				outQueryOffset = mCurrent.mInline[mCurrent.mCount];
			
			assert(mSeqCurrent - mSeqBegin - WORDSIZE < numeric_limits<uint16>::max());
			outTargetOffset = static_cast<uint16>(mSeqCurrent - mSeqBegin - WORDSIZE);
			result = true;
			break;
		}
		
		if (mSeqCurrent == mSeqEnd)
			break;
		
		int8 r = ResidueNr(*mSeqCurrent++);
		while (r < 0 or r >= kM6AACount and mSeqCurrent < mSeqEnd)
			r = ResidueNr(*mSeqCurrent++);

		if (mSeqCurrent == mSeqEnd)
			break;
		
		mIndex = ((mIndex & kMask) << kM6Bits) | *mSeqCurrent++;
		assert(mIndex < mLookup.size());
		mCurrent = mLookup[mIndex];
	}

	return result;
}

// --------------------------------------------------------------------

struct M6Diagonal
{
			M6Diagonal() : mQuery(0), mTarget(0) {}
			M6Diagonal(int16 inQuery, int16 inTarget)
				: mQuery(0), mTarget(0)
			{
				if (inQuery > inTarget)
					mQuery = inQuery - inTarget;
				else
					mTarget = -(inTarget - inQuery);
			}
			
	bool	operator<(const M6Diagonal& rhs) const
				{ return mQuery < rhs.mQuery or (mQuery == rhs.mQuery and mTarget < rhs.mTarget); }

	int16	mQuery, mTarget;
};

struct M6DiagonalStartTable
{
			M6DiagonalStartTable() : mTable(nullptr) {}
			~M6DiagonalStartTable() { delete[] mTable; }
	
	void	Reset(uint32 inQueryLength, uint32 inTargetLength)
			{
				mQueryLength = inQueryLength;
				mTargetLength = inTargetLength;
				
				uint32 n = mQueryLength + mTargetLength + 1;
				if (mTable == nullptr or n > mTableLength)
				{
					uint32 k = ((n / 10240) + 1) * 10240;
					int32* t = new int32[k];
					delete[] mTable;
					mTable = t;
					mTableLength = k;
				}
				
				memset(mTable, 0xf3, n * sizeof(int32));
			}

	int32&	operator[](const M6Diagonal& inD)
				{ return mTable[mTargetLength + inD.mTarget + inD.mQuery]; }

	int32*	mTable;
	int32	mTableLength, mTargetLength, mQueryLength;
};

// --------------------------------------------------------------------

template<int WORDSIZE>
void Search(const char* inFasta, size_t inLength, const string& inQuery)
{
	typedef M6WordHitIterator<WORDSIZE>	M6WordHitIterator;
	typedef M6WordHitIterator::WordHitIteratorStaticData M6StaticData;
	
	M6StaticData data;
	M6WordHitIterator::Init(inQuery.c_str(), inQuery.length(), kM6Blosum62, kM6Threshold, data);
	
	size_t offset = 0;
	while (*inFasta != '>')		// skip over 'header'
		++offset, ++inFasta, --inLength;
	const char* end = inFasta + inLength;
	uint32 queryLength = static_cast<uint32>(inQuery.length());
	
	M6WordHitIterator iter(data);
	M6DiagonalStartTable diagonals;
	
	int64 hitsToDb = 0, extensions = 0;
	
	while (inFasta != end)
	{
		const char* entry = inFasta;
		const char* seq = nullptr;
		uint32 seqLength = 0;

		while (inFasta != end and *++inFasta != '>')
		{
			if (seq == nullptr)
			{
				if (*inFasta == '\n')
					seq = inFasta + 1;
			}
			else if (*inFasta != '\r' and *inFasta != '\n' and *inFasta != ' ' and *inFasta != '\t')
				++seqLength;
		}
		
		iter.Reset(seq, inFasta);
		diagonals.Reset(queryLength, seqLength);
		
		uint16 queryOffset, targetOffset;
		while (iter.Next(queryOffset, targetOffset))
		{
			++hitsToDb;
			
			M6Diagonal d(queryOffset, targetOffset);
			int32 m = diagonals[d];
			
			int32 distance = queryOffset - m;
			if (m == kM6UnusedDiagonal or distance >= kM6HitWindow)
				diagonals[d] = queryOffset;
			else
			{
				++extensions;
			}
		}
	}
}


// --------------------------------------------------------------------

void QueryBlastDB(const fs::path& inFasta, const string& inQuery)
{
	io::mapped_file file(inFasta.string().c_str(), io::mapped_file::readonly);
	if (not file.is_open())
		throw M6Exception("FastA file %s not open", inFasta.string());
	
	const char* data = file.const_data();
	size_t length = file.size();
	
	Search<3>(data, length, inQuery);
	
}

}

// --------------------------------------------------------------------

int main()
{
	try
	{
//		BuildBlastDB();
		const char kQuery[] = "MSFCSFFGGEVFQNHFEPGVYVCAKCGYELFSSHSKYAHSSPWPAFTETIHADSVAKRPEHNRPGALKVSCGRCGNGLGHEFLNDGPKRGQSRFUIFSSSLKFIPKGQESSPSQGQ";
		M6Blast::QueryBlastDB(fs::path("C:/data/fasta/uniprot_sprot.fasta"), kQuery);
	}
	catch (exception& e)
	{
		cerr << e.what() << endl;
	}
	
	return 0;
}
