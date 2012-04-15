#include "M6Lib.h"

#include <iostream>

#include <zeep/xml/node.hpp>
#include <zeep/xml/document.hpp>
#include <zeep/xml/writer.hpp>

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/lexical_cast.hpp>
#include <boost/timer/timer.hpp>

#include "M6Blast.h"
#include "M6Error.h"
#include "M6Progress.h"
#include "M6SequenceFilter.h"

using namespace std;
using namespace zeep;
namespace fs = boost::filesystem;
namespace io = boost::iostreams;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

namespace M6Blast
{

typedef basic_string<uint8> sequence;

const uint32
	kM6AACount				= 22,	// 20 + B and Z
	kM6ResCount				= 23,	// includes X
	kM6Bits					= 5,
	kM6Threshold			= 11,
	kM6UngappedDropOff		= 7,
	kM6GappedDropOff		= 15,
	kM6GappedDropOffFinal	= 25,
	kM6GapTrigger			= 22;

const int32
	kM6HitWindow			= 40,
	kM6UnusedDiagonal		= 0xf3f3f3f3;

const double
	kLn2 = log(2.);

// 22 real letters and 1 dummy
const char kM6Residues[] = "ABCDEFGHIKLMNPQRSTVWYZX";

inline uint8 ResidueNr(char inAA)
{
	int result = -1;

	const static uint8 kResidueNrTable[] = {
	//	A   B   C   D   E   F   G   H   I       K   L   M   N       P   Q   R   S   T  U=X  V   W   X   Y   Z
		0,  1,  2,  3,  4,  5,  6,  7,  8, 23,  9, 10, 11, 12, 23, 13, 14, 15, 16, 17, 22, 18, 19, 22, 20, 21
	};
	
	inAA |= 040;
	if (inAA >= 'a' and inAA <= 'z')
		result = kResidueNrTable[inAA - 'a'];
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

struct M6MatrixData
{
	const char*	mName;
	int8		mGapOpen, mGapExtend;
	const int8*	mMatrix;
	double		mGappedStats[5], mUngappedStats[5];
} kM6MatrixData[] = {
	{ "BLOSUM62", 11, 1, kM6Blosum62, { 0.267, 0.041, 0.14, 1.9, -30 }, { 0.3176, 0.134, 0.4012, 0.7916, -3.2 } }
};

const int16
	kSentinalScore = -9999;

class M6Matrix
{
  public:
//					M6Matrix(const M6MatrixData& inData) : mData(inData) {}
					M6Matrix(const string& inName, int32 inGapOpen, int32 inGapExtend);

	int8			operator()(char inAA1, char inAA2) const;
	int8			operator()(uint8 inAA1, uint8 inAA2) const;

	int32			OpenCost() const		{ return mData.mGapOpen; }
	int32			ExtendCost() const		{ return mData.mGapExtend; }

	double			GappedLambda() const	{ return mData.mGappedStats[0]; }
	double			GappedKappa() const		{ return mData.mGappedStats[1]; }
	double			GappedEntropy() const	{ return mData.mGappedStats[2]; }
	double			GappedAlpha() const		{ return mData.mGappedStats[3]; }
	double			GappedBeta() const		{ return mData.mGappedStats[4]; }

	double			UngappedLambda() const	{ return mData.mUngappedStats[0]; }
	double			UngappedKappa() const	{ return mData.mUngappedStats[1]; }
	double			UngappedEntropy() const	{ return mData.mUngappedStats[2]; }
	double			UngappedAlpha() const	{ return mData.mUngappedStats[3]; }
	double			UngappedBeta() const	{ return mData.mUngappedStats[4]; }

  private:
	M6MatrixData	mData;
};

M6Matrix::M6Matrix(const string& inName, int32 inGapOpen, int32 inGapExtend)
{
	mData.mName = nullptr;
	for (int i = 0; i < sizeof(kM6MatrixData) / sizeof(M6MatrixData); ++i)
	{
		if (ba::iequals(inName, kM6MatrixData[i].mName) and
			inGapOpen == kM6MatrixData[i].mGapOpen and
			inGapExtend == kM6MatrixData[i].mGapExtend)
		{
			mData = kM6MatrixData[i];
			break;
		}
	}
	
	if (mData.mName == nullptr)
		throw M6Exception("Unsupported matrix/gap combination (%s/%d/%d)", inName.c_str(), inGapOpen, inGapExtend);
}

inline int8 M6Matrix::operator()(uint8 inAA1, uint8 inAA2) const
{
	int result = -4;
	
	if (inAA1 < kM6ResCount and inAA2 < kM6ResCount)
	{
		if (inAA1 >= inAA2)
			result = kM6Blosum62[(inAA1 * (inAA1 + 1)) / 2 + inAA2];
		else
			result = kM6Blosum62[(inAA2 * (inAA2 + 1)) / 2 + inAA1];
	}

	return result;	
}

inline int8 M6Matrix::operator()(char inAA1, char inAA2) const
{
	return operator()(ResidueNr(inAA1), ResidueNr(inAA2));
}

// --------------------------------------------------------------------

namespace ncbi
{

/** 
 * Computes the adjustment to the lengths of the query and database sequences
 * that is used to compensate for edge effects when computing evalues. 
 *
 * The length adjustment is an integer-valued approximation to the fixed
 * point of the function
 *
 *    f(ell) = beta + 
 *               (alpha/lambda) * (log K + log((m - ell)*(n - N ell)))
 *
 * where m is the query length n is the length of the database and N is the
 * number of sequences in the database. The values beta, alpha, lambda and
 * K are statistical, Karlin-Altschul parameters.
 * 
 * The value of the length adjustment computed by this routine, A, 
 * will always be an integer smaller than the fixed point of
 * f(ell). Usually, it will be the largest such integer.  However, the
 * computed length adjustment, A, will also be so small that 
 *
 *    K * (m - A) * (n - N * A) > min(m,n).
 *
 * Moreover, an iterative method is used to compute A, and under
 * unusual circumstances the iterative method may not converge. 
 *
 * @param K      the statistical parameter K
 * @param logK   the natural logarithm of K
 * @param alpha_d_lambda    the ratio of the statistical parameters 
 *                          alpha and lambda (for ungapped alignments, the
 *                          value 1/H should be used)
 * @param beta              the statistical parameter beta (for ungapped
 *                          alignments, beta == 0)
 * @param query_length      the length of the query sequence
 * @param db_length         the length of the database
 * @param db_num_seq        the number of sequences in the database
 * @param length_adjustment the computed value of the length adjustment [out]
 *
 * @return   0 if length_adjustment is known to be the largest integer less
 *           than the fixed point of f(ell); 1 otherwise.
 */

int32 BlastComputeLengthAdjustment(
	double K, double alpha_d_lambda, double beta,
	int32 query_length, int64 db_length, int32 db_num_seqs,
	int32& length_adjustment)
{
	double logK = log(K);
	
    int32 i;                     /* iteration index */
    const int32 maxits = 20;     /* maximum allowed iterations */
    double m = query_length, n = static_cast<double>(db_length), N = db_num_seqs;

    double ell;            /* A float value of the length adjustment */
    double ss;             /* effective size of the search space */
    double ell_min = 0, ell_max;   /* At each iteration i,
                                         * ell_min <= ell <= ell_max. */
    bool converged    = false;       /* True if the iteration converged */
    double ell_next = 0;   /* Value the variable ell takes at iteration
                                 * i + 1 */
    /* Choose ell_max to be the largest nonnegative value that satisfies
     *
     *    K * (m - ell) * (n - N * ell) > max(m,n)
     *
     * Use quadratic formula: 2 c /( - b + sqrt( b*b - 4 * a * c )) */
    { /* scope of a, mb, and c, the coefficients in the quadratic formula
       * (the variable mb is -b) */
        double a  = N;
        double mb = m * N + n;
        double c  = n * m - std::max(m, n) / K;

        if(c < 0) {
            length_adjustment = 0;
            return 1;
        } else {
            ell_max = 2 * c / (mb + sqrt(mb * mb - 4 * a * c));
        }
    } /* end scope of a, mb and c */

    for(i = 1; i <= maxits; i++) {      /* for all iteration indices */
        double ell_bar;    /* proposed next value of ell */
        ell      = ell_next;
        ss       = (m - ell) * (n - N * ell);
        ell_bar  = alpha_d_lambda * (logK + log(ss)) + beta;
        if(ell_bar >= ell) { /* ell is no bigger than the true fixed point */
            ell_min = ell;
            if(ell_bar - ell_min <= 1.0) {
                converged = true;
                break;
            }
            if(ell_min == ell_max) { /* There are no more points to check */
                break;
            }
        } else { /* else ell is greater than the true fixed point */
            ell_max = ell;
        }
        if(ell_min <= ell_bar && ell_bar <= ell_max) { 
          /* ell_bar is in range. Accept it */
            ell_next = ell_bar;
        } else { /* else ell_bar is not in range. Reject it */
            ell_next = (i == 1) ? ell_max : (ell_min + ell_max) / 2;
        }
    } /* end for all iteration indices */
    if(converged) { /* the iteration converged */
        /* If ell_fixed is the (unknown) true fixed point, then we
         * wish to set length_adjustment to floor(ell_fixed).  We
         * assume that floor(ell_min) = floor(ell_fixed) */
        length_adjustment = (int32) ell_min;
        /* But verify that ceil(ell_min) != floor(ell_fixed) */
        ell = ceil(ell_min);
        if( ell <= ell_max ) {
          ss = (m - ell) * (n - N * ell);
          if(alpha_d_lambda * (logK + log(ss)) + beta >= ell) {
            /* ceil(ell_min) == floor(ell_fixed) */
            length_adjustment = (int32) ell;
          }
        }
    } else { /* else the iteration did not converge. */
        /* Use the best value seen so far */
        length_adjustment = (int32) ell_min;
    }

    return converged ? 0 : 1;
}

int32 BlastComputeLengthAdjustment(const M6Matrix& inMatrix, int32 query_length, int64 db_length, int32 db_num_seqs)
{
	int32 lengthAdjustment;
	(void)BlastComputeLengthAdjustment(inMatrix.GappedKappa(), inMatrix.GappedAlpha() / inMatrix.GappedLambda(),
		inMatrix.GappedBeta(), query_length, db_length, db_num_seqs, lengthAdjustment);
	return lengthAdjustment;
}

} // namespace ncbi

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

					M6Word(const uint8* inSequence)
					{
						for (uint32 i = 0; i < WORDSIZE; ++i)
							aa[i] = inSequence[i];
						aa[WORDSIZE] = 0;
					}

	uint8&			operator[](uint32 ix)	{ return aa[ix]; }
	const uint8*	c_str() const			{ return aa; }
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
			w[i] = resNr;
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
	static const uint32 kMask;
	
	struct Entry
	{
		uint16					mCount;
		uint16					mDataOffset;
	};

  public:

	typedef M6Word<WORDSIZE>						M6Word;
	typedef typename M6Word::PermutationIterator	M6WordPermutationIterator;

	struct M6WordHitIteratorStaticData
	{
		vector<Entry>			mLookup;
		vector<uint16>			mOffsets;
	};
								M6WordHitIterator(M6WordHitIteratorStaticData& inStaticData)
									: mLookup(inStaticData.mLookup), mOffsets(inStaticData.mOffsets) {}
	
	static void					Init(const sequence& inQuery, const M6Matrix& inMatrix,
									uint32 inThreshhold, M6WordHitIteratorStaticData& outStaticData);

	void						Reset(const sequence& inTarget);
	bool						Next(uint16& outQueryOffset, uint16& outTargetOffset);
	uint32						Index() const		{ return mIndex; }

  private:

	const uint8*	mTargetBegin;
	const uint8*	mTargetCurrent;
	const uint8*	mTargetEnd;
	vector<Entry>&	mLookup;
	vector<uint16>&	mOffsets;
	uint32			mIndex;
	Entry			mCurrent;
};

template<> const uint32 M6WordHitIterator<3>::kMask = 0x03FF;

template<int WORDSIZE>
void M6WordHitIterator<WORDSIZE>::Init(const sequence& inQuery, 
	const M6Matrix& inMatrix, uint32 inThreshhold, M6WordHitIteratorStaticData& outStaticData)
{
	uint64 N = M6Word::kM6MaxWordIndex;
	size_t M = 0;
	
	vector<vector<uint16>> test(N);
	
	for (uint16 i = 0; i < inQuery.length() - WORDSIZE + 1; ++i)
	{
		M6Word w(inQuery.c_str() + i);
		
		M6WordPermutationIterator p(w, inMatrix, inThreshhold);
		uint32 ix;

		while (p.Next(ix))
		{
			test[ix].push_back(i);
			++M;
		}
	}

	outStaticData.mLookup = vector<Entry>(N);
	outStaticData.mOffsets = vector<uint16>(M);

	uint16* data = &outStaticData.mOffsets[0];

	for (uint32 i = 0; i < N; ++i)
	{
		outStaticData.mLookup[i].mCount = static_cast<uint16>(test[i].size());
		outStaticData.mLookup[i].mDataOffset = static_cast<uint16>(data - &outStaticData.mOffsets[0]);

		for (uint32 j = 0; j < outStaticData.mLookup[i].mCount; ++j)
			*data++ = test[i][j];
	}
	
	assert(data < &outStaticData.mOffsets[0] + M);
}

template<int WORDSIZE>
void M6WordHitIterator<WORDSIZE>::Reset(const sequence& inTarget)
{
	mTargetBegin = mTargetCurrent = inTarget.c_str();
	mTargetEnd = mTargetBegin + inTarget.length();
	
	mIndex = 0;
	mCurrent.mCount = 0;

	for (uint32 i = 0; i < WORDSIZE - 1 and mTargetCurrent != mTargetEnd; ++i)
		mIndex = mIndex << kM6Bits | *mTargetCurrent++;
}

template<int WORDSIZE>
bool M6WordHitIterator<WORDSIZE>::Next(uint16& outQueryOffset, uint16& outTargetOffset)
{
	bool result = false;
	
	for (;;)
	{
		if (mCurrent.mCount-- > 0)
		{
			outQueryOffset = mOffsets[mCurrent.mDataOffset + mCurrent.mCount];
			outTargetOffset = static_cast<uint16>(mTargetCurrent - mTargetBegin - WORDSIZE);
			result = true;
			break;
		}
		
		if (mTargetCurrent == mTargetEnd)
			break;
		
		mIndex = ((mIndex & kMask) << kM6Bits) | *mTargetCurrent++;
		mCurrent = mLookup[mIndex];
	}

	return result;
}

// --------------------------------------------------------------------

struct M6Diagonal
{
			M6Diagonal() : mQuery(0), mTarget(0) {}
			M6Diagonal(int32 inQuery, int32 inTarget)
				: mQuery(0), mTarget(0)
			{
				if (inQuery > inTarget)
					mQuery = inQuery - inTarget;
				else
					mTarget = -(inTarget - inQuery);
			}
			
	bool	operator<(const M6Diagonal& rhs) const
				{ return mQuery < rhs.mQuery or (mQuery == rhs.mQuery and mTarget < rhs.mTarget); }

	int32	mQuery, mTarget;
};

struct M6DiagonalStartTable
{
			M6DiagonalStartTable() : mTable(nullptr) {}
			~M6DiagonalStartTable() { delete[] mTable; }
	
	void	Reset(uint32 inQueryLength, uint32 inTargetLength)
			{
				mQueryLength = inQueryLength;
				mTargetLength = inTargetLength;
				
				int32 n = mQueryLength + mTargetLength + 1;
				if (mTable == nullptr or n >= mTableLength)
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
			{
				assert(inD.mQuery < mQueryLength);
				assert(inD.mTarget < mTargetLength);
				assert(mTargetLength + inD.mTarget + inD.mQuery < mTableLength);
				assert(mTargetLength + inD.mTarget + inD.mQuery >= 0);
				return mTable[mTargetLength + inD.mTarget + inD.mQuery];
			}

  private:
							M6DiagonalStartTable(const M6DiagonalStartTable&);
	M6DiagonalStartTable&	operator=(const M6DiagonalStartTable&);

	int32*	mTable;
	int32	mTableLength, mTargetLength, mQueryLength;
};

// --------------------------------------------------------------------

struct M6DPData
{
				M6DPData(size_t inDimX, size_t inDimY) : mDimX(inDimX), mDimY(inDimY)
				{
					mM6DPDataLength = (inDimX + 1) * (inDimY + 1);
					mM6DPData = reinterpret_cast<int16*>(calloc(mM6DPDataLength, sizeof(int16)));
				}
				~M6DPData()												{ free(mM6DPData); }

	int16		operator()(uint32 inI, uint32 inJ) const			{ return mM6DPData[inI * mDimY + inJ]; }
	int16&		operator()(uint32 inI, uint32 inJ)					{ return mM6DPData[inI * mDimY + inJ]; }
	
	int16*		mM6DPData;
	size_t		mM6DPDataLength;
	size_t		mDimX;
	size_t		mDimY;
};

struct M6RecordTraceBack
{
				M6RecordTraceBack(M6DPData& inTraceBack) : mTraceBack(inTraceBack) { }

	int16		operator()(int16 inB, int16 inIx, int16 inIy, uint32 inI, uint32 inJ)
				{
					int16 result;

					if (inB >= inIx and inB >= inIy)
					{
						result = inB;
						mTraceBack(inI, inJ) = 0;
					}
					else if (inIx >= inB and inIx >= inIy)
					{
						result = inIx;
						mTraceBack(inI, inJ) = 1;
					}
					else
					{
						result = inIy;
						mTraceBack(inI, inJ) = -1;
					}
					
					return result;
				}
	
	void		Set(uint32 inI, uint32 inJ, int16 inD)			{ mTraceBack(inI, inJ) = inD; }

	M6DPData&	mTraceBack;
};

// --------------------------------------------------------------------

inline void ReadEntry(const char*& inFasta, const char* inEnd, sequence& outTarget)
{
	assert(inFasta == inEnd or *inFasta == '>');
	
	while (inFasta != inEnd and *inFasta++ != '\n')
		;

	outTarget.clear();

	bool nl = false;
	while (inFasta != inEnd)
	{
		char ch = *inFasta++;
		
		if (ch == '\n')
			nl = true;
		else if (ch == '>' and nl)
		{
			--inFasta;
			break;
		}
		else
		{
			uint8 rn = ResidueNr(ch);
			if (rn < kM6ResCount)
				outTarget += rn;
			nl = false;
		}
	}
}

// --------------------------------------------------------------------

struct M6Hsp
{
	uint32		mScore;
	uint32		mQueryStart, mQueryEnd, mTargetStart, mTargetEnd;
	sequence	mAlignedQuery, mAlignedTarget;
	double		mBitScore;
	double		mExpect;
	bool		mGapped;

	bool		operator>(const M6Hsp& inHsp) const				{ return mScore > inHsp.mScore; }
	void		CalculateExpect(int64 inSearchSpace, double inLambda, double inLogKappa);
	bool		Overlaps(const M6Hsp& inOther) const
				{
					return
						mQueryEnd >= inOther.mQueryStart and mQueryStart <= inOther.mQueryEnd and
						mTargetEnd >= inOther.mTargetStart and mTargetStart <= inOther.mTargetEnd;
				}
};

void M6Hsp::CalculateExpect(int64 inSearchSpace, double inLambda, double inLogKappa)
{
	mBitScore = floor((inLambda * mScore - inLogKappa) / kLn2);
	mExpect = inSearchSpace / pow(2., mBitScore);
}

//xml::node* M6Hsp::ToXML(uint32 inIndex, const M6Matrix& inMatrix, int64 inSearchSpace) const
//{
////	CalculateMidline(mAlignedQuery, inMatrix, false);
//	xml::element* result = new xml::element("Hsp");
//	xml::element* child;
//	
//	auto fmt = [](uint8 rn) -> char {
//		return rn == '-' ? '-' : kM6Residues[rn];
//	};
//
//	string alignedQuery(mAlignedQuery.length(), 0);
//	transform(mAlignedQuery.begin(), mAlignedQuery.end(), alignedQuery.begin(), fmt);
//
//	string alignedTarget(mAlignedTarget.length(), 0);
//	transform(mAlignedTarget.begin(), mAlignedTarget.end(), alignedTarget.begin(), fmt);
//	
//	result->append(child = new xml::element("Hsp_num"));			child->content(boost::lexical_cast<string>(inIndex));
//	result->append(child = new xml::element("Hsp_bit-score"));		child->content(boost::lexical_cast<string>(mBitScore));
//	result->append(child = new xml::element("Hsp_score"));			child->content(boost::lexical_cast<string>(mScore));
//	result->append(child = new xml::element("Hsp_evalue"));			child->content(boost::lexical_cast<string>(mExpect));
//	result->append(child = new xml::element("Hsp_query-from"));		child->content(boost::lexical_cast<string>(mQueryStart + 1));
//	result->append(child = new xml::element("Hsp_query-to"));		child->content(boost::lexical_cast<string>(mQueryEnd));
//	
//	result->append(child = new xml::element("Hsp_qseq"));			child->content(alignedQuery);
//	result->append(child = new xml::element("Hsp_hseq"));			child->content(alignedTarget);
////	result->append(child = new xml::element("Hsp_midline"));		child->content(mMidline);
//	
//	return result;
//}

struct M6Hit
{
					M6Hit(const char* inEntry, const sequence& inTarget)
						: mEntry(inEntry), mTarget(inTarget) {}

	void			AddHsp(const M6Hsp& inHsp);
	void			Cleanup(int64 inSearchSpace, double inLambda, double inLogKappa, double inExpect);

//	xml::node*		ToXML(uint32 inIndex, const M6Matrix& inMatrix, int64 inSearchSpace) const;
	
	const char*		mEntry;
	sequence		mTarget;
	vector<M6Hsp>	mHsps;
};

void M6Hit::AddHsp(const M6Hsp& inHsp)
{
	bool found = false;

	foreach (auto& hsp, mHsps)
	{
		if (inHsp.Overlaps(hsp))
		{
			if (hsp.mScore < inHsp.mScore)
				hsp = inHsp;
			found = true;
			break;
		}
	}

	if (not found)
		mHsps.push_back(inHsp);
}

void M6Hit::Cleanup(int64 inSearchSpace, double inLambda, double inLogKappa, double inExpect)
{
	sort(mHsps.begin(), mHsps.end(), greater<M6Hsp>());
	
	vector<M6Hsp>::iterator a = mHsps.begin();
	while (a != mHsps.end() and a + 1 != mHsps.end())
	{
		vector<M6Hsp>::iterator b = a + 1;
		while (b != mHsps.end())
		{
			if (a->Overlaps(*b))
				b = mHsps.erase(b);
			else
				++b;
		}
		++a;
	}

	for_each(mHsps.begin(), mHsps.end(), [=](M6Hsp& hsp) {
		hsp.CalculateExpect(inSearchSpace, inLambda, inLogKappa);
	});
	
	mHsps.erase(
		remove_if(mHsps.begin(), mHsps.end(), [=](const M6Hsp& hsp) -> bool {
			return hsp.mExpect > inExpect;
		}),
		mHsps.end());
}

//xml::node* M6Hit::ToXML(uint32 inIndex, const M6Matrix& inMatrix, int64 inSearchSpace) const
//{
//	xml::element* result = new xml::element("Hit");
//	xml::element* child;
//	
//	result->append(child = new xml::element("Hit_num"));	child->content(boost::lexical_cast<string>(inIndex));
//
//	string def(mEntry, strchr(mEntry, '\n'));
//	result->append(child = new xml::element("Hit_def"));	child->content(def);
//	result->append(child = new xml::element("Hit_len"));	child->content(boost::lexical_cast<string>(mTargetLength));
//	result->append(child = new xml::element("Hit_hsps"));
//	
//	int nr = 1;
//	foreach (auto& hsp, mHsps)
//		child->append(hsp.ToXML(nr++, inMatrix, inSearchSpace));
//	
//	return result;
//}

// --------------------------------------------------------------------

template<int WORDSIZE>
class M6BlastQuery
{
  public:
					M6BlastQuery(const string& inQuery, bool inFilter, double inExpect,
						bool inGapped, const M6Matrix& inMatrix, uint32 inReportLimit);

	void			Search(const char* inFasta, size_t inLength);
	void			Report(Result& outResult);

  private:

	void			SearchPart(const char* inFasta, size_t inLength);

	int32			Extend(int32& ioQueryStart, const sequence& inTarget, int32& ioTargetStart, int32& ioDistance);
	template<class Iterator1, class Iterator2, class TraceBack>
	int32			AlignGapped(Iterator1 inQueryBegin, Iterator1 inQueryEnd,
						Iterator2 inTargetBegin, Iterator2 inTargetEnd,
						TraceBack& inTraceBack, int32 inDropOff, uint32& outBestX, uint32& outBestY);
	int32			AlignGapped(const sequence& inTarget, M6Hsp& ioHsp);

	void			AddHit(M6Hit* inHit);

	typedef M6WordHitIterator<WORDSIZE>								M6WordHitIterator;
	typedef typename M6WordHitIterator::M6WordHitIteratorStaticData	M6StaticData;

	string			mUnfiltered;
	sequence		mQuery;
	const M6Matrix&	mMatrix;
	double			mExpect, mCutOff;
	bool			mGapped;
	int32			mS1, mS2, mXu, mXg, mXgFinal;
	uint32			mReportLimit;
	
	uint32			mDbCount;
	int64			mDbLength, mSearchSpace;
	
	vector<M6Hit*>	mHits;
	
	M6StaticData	mWordHitData;
};

template<int WORDSIZE>
M6BlastQuery<WORDSIZE>::M6BlastQuery(const string& inQuery, bool inFilter, double inExpect,
		bool inGapped, const M6Matrix& inMatrix, uint32 inReportLimit)
	: mUnfiltered(inQuery), mQuery(inQuery.length(), 0), mMatrix(inMatrix), mExpect(inExpect), mGapped(inGapped), mReportLimit(inReportLimit)
	, mDbCount(0), mDbLength(0), mSearchSpace(0)
{
	if (mQuery.length() >= numeric_limits<uint16>::max())
		throw M6Exception("Query length exceeds maximum");
	
	string query(inQuery);
	if (inFilter)
		SEG(query);
	
	transform(query.begin(), query.end(), mQuery.begin(), [](char aa) -> uint8 {
		return ResidueNr(aa); 
	});
	
	mXu =		static_cast<int32>(ceil((kLn2 * kM6UngappedDropOff) / mMatrix.UngappedLambda()));
	mXg =		static_cast<int32>((kLn2 * kM6GappedDropOff) / mMatrix.GappedLambda());
	mXgFinal =	static_cast<int32>((kLn2 * kM6GappedDropOffFinal) / mMatrix.GappedLambda());
	mS1 =		static_cast<int32>((kLn2 * kM6GapTrigger + log(mMatrix.UngappedKappa())) / mMatrix.UngappedLambda());
	
	// we're not using S2
	mS2 =		static_cast<int32>((kLn2 * kM6GapTrigger + log(mMatrix.GappedKappa())) / mMatrix.GappedLambda());;	// yeah, that sucks... perhaps

	M6WordHitIterator::Init(mQuery, mMatrix, kM6Threshold, mWordHitData);
}

template<int WORDSIZE>
void M6BlastQuery<WORDSIZE>::Search(const char* inFasta, size_t inLength)
{
	boost::timer::auto_cpu_timer t;

	SearchPart(inFasta, inLength);
	
	int32 lengthAdjustment = ncbi::BlastComputeLengthAdjustment(mMatrix, static_cast<uint32>(mQuery.length()), mDbLength, mDbCount);

	int64 effectiveQueryLength = mQuery.length() - lengthAdjustment;
	int64 effectiveDbLength = mDbLength - mDbCount * lengthAdjustment;

	mSearchSpace = effectiveDbLength * effectiveQueryLength;
	
	foreach (M6Hit* hit, mHits)
	{
		foreach (M6Hsp& hsp, hit->mHsps)
		{
			if (mGapped)
			{
				uint32 newScore = AlignGapped(hit->mTarget, hsp);
				assert(hsp.mAlignedQuery.length() == hsp.mAlignedTarget.length());
				
				if (hsp.mScore < newScore)
					hsp.mScore = newScore;
				
//				if (hsp.mGapped)
//					++mGapCount;
			}
			//else
			//{
			//	hsp.mAlignedQuery = mQuery.substr(hsp.mQueryStart, hsp.mQueryEnd - hsp.mQueryStart);
			//	hsp.mAlignedTarget = hsp.mTarget.substr(hsp.mTargetStart, hsp.mTargetEnd - hsp.mTargetStart);
			//}
		}
		
		hit->Cleanup(mSearchSpace, mMatrix.GappedLambda(), log(mMatrix.GappedKappa()), mExpect);
	}

	mHits.erase(
		remove_if(mHits.begin(), mHits.end(), [](const M6Hit* hit) -> bool { return hit->mHsps.empty(); }),
		mHits.end());

	sort(mHits.begin(), mHits.end(), [](const M6Hit* a, const M6Hit* b) -> bool {
		return a->mHsps.front().mScore > b->mHsps.front().mScore;
	});

	if (mHits.size() > mReportLimit)
	{
		for_each(mHits.begin() + mReportLimit, mHits.end(), [](M6Hit* hit) { delete hit; });
		mHits.erase(mHits.begin() + mReportLimit, mHits.end());
	}
}

template<int WORDSIZE>
void M6BlastQuery<WORDSIZE>::Report(Result& outResult)
{
	outResult.mDbCount = mDbCount;
	outResult.mDbLength = mDbLength;
	outResult.mEffectiveSpace = mSearchSpace;
	outResult.mKappa = mMatrix.GappedKappa();
	outResult.mLambda = mMatrix.GappedLambda();
	outResult.mEntropy = mMatrix.GappedEntropy();
	
	foreach (M6Hit* hit, mHits)
	{
		Hit h;
		static const boost::regex re("^>(\\w+)((?:\\|([^|]*))?(?:\\|([^|]+))?(?:\\|([^|]+))?(?:\\|([^|]+))?) (.+)");
		boost::smatch m;
		h.mDefLine = string(hit->mEntry + 1, strchr(hit->mEntry + 1, '\n'));

		if (boost::regex_match(h.mDefLine, m, re, boost::match_not_dot_newline))
		{
			if (m[1] == "sp")
			{
				h.mAccession = m[3];
				h.mID = m[4];
			}
			else
				h.mID = m[2];

			h.mDefLine = m[6];
		}
		else
			h.mDefLine.erase(0, 1);
		
		foreach (M6Hsp& hsp, hit->mHsps)
		{
			Hsp p = { hsp.mQueryStart + 1, hsp.mQueryEnd, hsp.mTargetStart + 1, hsp.mTargetEnd,
				hit->mTarget.length(), hsp.mScore, hsp.mBitScore, hsp.mExpect };
			
			string::const_iterator q = mUnfiltered.begin() + hsp.mQueryStart;
			foreach (uint8 r, hsp.mAlignedTarget)
			{
				char qaa = *q++;
				
				if (qaa == kM6Residues[r])
				{
					++p.mIdentity;
					++p.mPositive;
					p.mMidLine += qaa;
				}
				else if (mMatrix(ResidueNr(qaa), r) > 0)
				{
					++p.mPositive;
					p.mMidLine += '+';
				}
				else
				{
					if (qaa == '-' or r == '-')
						++p.mGaps;
					p.mMidLine += ' ';
				}
			}

			h.mHsps.push_back(p);
		}
		outResult.mHits.push_back(h);
	}
}

template<int WORDSIZE>
void M6BlastQuery<WORDSIZE>::SearchPart(const char* inFasta, size_t inLength)
{
	boost::timer::auto_cpu_timer t;

	size_t offset = 0;
	while (*inFasta != '>')		// skip over 'header'
		++offset, ++inFasta, --inLength;
	const char* end = inFasta + inLength;
	uint32 queryLength = static_cast<uint32>(mQuery.length());
	
	M6WordHitIterator iter(mWordHitData);
	M6DiagonalStartTable diagonals;
	sequence target;
	
	int64 hitsToDb = 0, extensions = 0, successfulExtensions = 0, gappedAlignmentAttempts = 0, successfulGappedAlignments = 0,
		dbLength = 0;
	uint32 dbCount = 0;

	unique_ptr<M6Hit> hit;
	
	while (inFasta != end)
	{
		if (hit)
			AddHit(hit.release());
		
		const char* entry = inFasta;
		ReadEntry(inFasta, end, target);
		if (target.empty())
			continue;
		
		dbCount += 1;
		dbLength += target.length();
		
		iter.Reset(target);
		diagonals.Reset(queryLength, static_cast<uint32>(target.length()));
		
		uint16 queryOffset, targetOffset;
		while (iter.Next(queryOffset, targetOffset))
		{
			++hitsToDb;
			
			M6Diagonal d(queryOffset, targetOffset);
			int32 m = diagonals[d];
			
			int32 distance = queryOffset - m;

			if (m == kM6UnusedDiagonal or distance >= kM6HitWindow)
				diagonals[d] = queryOffset;
			else if (distance > WORDSIZE)
			{
				int32 queryStart = m;
				int32 targetStart = targetOffset - distance;
				int32 alignmentDistance = distance + WORDSIZE;

				if (targetStart < 0 or queryStart < 0)
					continue;
	
				++extensions;
				
				int32 score = Extend(queryStart, target, targetStart, alignmentDistance);
	
				if (score >= mS1)
				{
					++successfulExtensions;
	
					M6Hsp hsp;
					hsp.mScore = score;

					// extension results, to be updated later
					hsp.mQueryStart = queryStart;
					hsp.mQueryEnd = queryStart + alignmentDistance;
					hsp.mTargetStart = targetStart;
					hsp.mTargetEnd = targetStart + alignmentDistance;

					if (hit.get() == nullptr)
						hit.reset(new M6Hit(entry, target));

					hit->AddHsp(hsp);
				}
				
				diagonals[d] = queryStart + alignmentDistance;
			}
		}
	}

	if (hit)
		AddHit(hit.release());

//	cout << "hits to db: " << hitsToDb << endl
//		 << "extensions: " << extensions << endl
//		 << "successful extensions: " << successfulExtensions << endl
//		 << "gapped: " << gappedAlignmentAttempts << endl
//		 << "X1: " << mXu << endl
//		 << "X2: " << mXg << endl
//		 << "X3: " << mXgFinal << endl
//		 << "S1: " << mS1 << endl
//		 << "S2: " << mS2 << endl;
	
	// LOCK
	mDbCount += dbCount;
	mDbLength += dbLength;
}

template<int WORDSIZE>
int32 M6BlastQuery<WORDSIZE>::Extend(int32& ioQueryStart, const sequence& inTarget, int32& ioTargetStart, int32& ioDistance)
{
	// use iterators
	sequence::const_iterator ai = mQuery.begin() + ioQueryStart;
	sequence::const_iterator bi = inTarget.begin() + ioTargetStart;
	
	int32 score = 0;
	for (int i = 0; i < ioDistance; ++i, ++ai, ++bi)
		score += mMatrix(*ai, *bi);
	
	// record start and stop positions for optimal score
	sequence::const_iterator qe = ai;
	
	for (int32 test = score, n = static_cast<int32>(min(mQuery.end() - ai, inTarget.end() - bi));
		 test >= score - mXu and n > 0;
		 --n, ++ai, ++bi)
	{
		test += mMatrix(*ai, *bi);
		
		if (test > score)
		{
			score = test;
			qe = ai;
		}
	}
	
	ai = mQuery.begin() + ioQueryStart;
	bi = inTarget.begin() + ioTargetStart;
	sequence::const_iterator qs = ai + 1;

	for (int32 test = score, n = min(ioQueryStart, ioTargetStart);
		 test >= score - mXu and n > 0;
		 --n)
	{
		test += mMatrix(*--ai, *--bi);

		if (test > score)
		{
			score = test;
			qs = ai;
		}
	}
	
	int32 delta = static_cast<int32>(ioQueryStart - (qs - mQuery.begin()));
	ioQueryStart -= delta;
	ioTargetStart -= delta;
	ioDistance = static_cast<int32>(qe - qs);
	
	return score;
}

template<int WORDSIZE>
template<class Iterator1, class Iterator2, class TraceBack>
int32 M6BlastQuery<WORDSIZE>::AlignGapped(
	Iterator1 inQueryBegin, Iterator1 inQueryEnd, Iterator2 inTargetBegin, Iterator2 inTargetEnd,
	TraceBack& inTraceBack, int32 inDropOff, uint32& outBestX, uint32& outBestY)
{
	const M6Matrix& s = mMatrix;	// for readability
	TraceBack& tb_max = inTraceBack;
	int32 d = s.OpenCost();
	int32 e = s.ExtendCost();
	
	uint32 dimX = static_cast<uint32>(inQueryEnd - inQueryBegin);
	uint32 dimY = static_cast<uint32>(inTargetEnd - inTargetBegin);
	
	M6DPData B(dimX, dimY);
	M6DPData Ix(dimX, dimY);
	M6DPData Iy(dimX, dimY);
	
	int32 bestScore = 0;
	uint32 bestX;
	uint32 bestY;
	uint32 colStart = 1;
	uint32 lastColStart = 1;
	uint32 colEnd = dimY;
	
	// first column
	uint32 i = 1, j = 1;
	Iterator1 x = inQueryBegin;
	Iterator2 y = inTargetBegin;

	// first cell
	int32 Ix1 = kSentinalScore, Iy1 = kSentinalScore;

	// (1)
	int32 M = s(*x, *y);
	
	// (2)
	(void)tb_max(M, kSentinalScore, kSentinalScore, 1, 1);
	bestScore = B(1, 1) = M;
	bestX = bestY = 1;

	// (3)
	Ix(1, 1) = M - d;

	// (4)
	Iy(1, 1) = M - d;

	// remaining cells in the first column
	y = inTargetBegin + 1;
	Ix1 = kSentinalScore;
	M = kSentinalScore;

	for (j = 2; y != inTargetEnd; ++j, ++y)
	{
		Iy1 = Iy(i, j - 1);
		
		// (2)
		int32 Bij = B(i, j) = Iy1;
		tb_max.Set(i, j, -1);

		// (3)
		Ix(i, j) = kSentinalScore;
		
		// (4)
		Iy(i, j) = Iy1 - e;

		if (Bij < bestScore - inDropOff)
		{
			colEnd = j;
			break;
		}
	}

	// remaining columns
	++x;
	for (i = 2; x != inQueryEnd and colEnd >= colStart; ++i, ++x)
	{
		y = inTargetBegin + colStart - 1;
		uint32 newColStart = colStart;
		bool beforeFirstRow = true;
		
		for (j = colStart; y != inTargetEnd; ++j, ++y)
		{
			Ix1 = kSentinalScore;
			Iy1 = kSentinalScore;
			
			if (j < colEnd)
				Ix1 = Ix(i - 1, j);
			
			if (j > colStart)
				Iy1 = Iy(i, j - 1);
			
			// (1)
			if (j <= lastColStart or j > colEnd)
				M = kSentinalScore;
			else
				M = B(i - 1, j - 1) + s(*x, *y);

			// cut off the max value
//			assert(M < numeric_limits<INT>::max());
			if (M > numeric_limits<int16>::max())
				M = numeric_limits<int16>::max();
			
			// (2)
			int32 Bij = B(i, j) = tb_max(M, Ix1, Iy1, i, j);

			// (3)
			Ix(i, j) = max(M - d, Ix1 - e);
			
			// (4)
			Iy(i, j) = max(M - d, Iy1 - e);

			if (Bij > bestScore)
			{
				bestScore = Bij;
				bestX = i;
				bestY = j;
				beforeFirstRow = false;
			}
			else if (Bij < bestScore - inDropOff)
			{
				if (beforeFirstRow)
				{
					newColStart = j;
					if (newColStart > colEnd)
						break;
				}
				else if (j > bestY + 1)
				{
					colEnd = j;
					break;
				}
			}
			else
			{
				beforeFirstRow = false;
				if (j > colEnd)
					colEnd = j;
			}
		}
		
		lastColStart = colStart;
		colStart = newColStart;
	}
	
	outBestY = bestY;
	outBestX = bestX;
	
	return bestScore;
}

template<int WORDSIZE>
int32 M6BlastQuery<WORDSIZE>::AlignGapped(const sequence& inTarget, M6Hsp& ioHsp)
{
	uint32 x, y;
	int32 score = 0;
	
	ioHsp.mGapped = false;

	sequence alignedQuery;
	sequence alignedTarget;

	uint32 targetSeed = (ioHsp.mTargetStart + ioHsp.mTargetEnd) / 2;
	uint32 querySeed = (ioHsp.mQueryStart + ioHsp.mQueryEnd) / 2;

	// start with the part before the seed
	M6DPData d1(querySeed + 1, targetSeed + 1);
	M6RecordTraceBack tbb(d1);

	score = AlignGapped(
		mQuery.rbegin() + (mQuery.length() - querySeed), mQuery.rend(),
		inTarget.rbegin() + (inTarget.length() - targetSeed), inTarget.rend(),
		tbb, mXgFinal, x, y);
	ioHsp.mQueryStart = querySeed - x;
	ioHsp.mTargetStart = targetSeed - y;
	
	sequence::const_iterator qi = mQuery.begin() + querySeed - x, qis = qi;
	sequence::const_iterator si = inTarget.begin() + targetSeed - y, sis = si;
	
	uint32 qLen = 1;
	uint32 sLen = 1;
	
	while (x >= 1 and y >= 1)
	{
		if (x >= 1 and y >= 1 and d1(x, y) == 0)
		{
			alignedQuery += *qi++;
			alignedTarget += *si++;
			--x;
			--y;
		}
		else if (y >= 1 and d1(x, y) < 0)
		{
			alignedQuery += '-';
			alignedTarget += *si++;
			--y;
			ioHsp.mGapped = true;
		}
		else // if (x >= 1 and d1(x, y) > 0)
		{
			alignedQuery += *qi++;
			alignedTarget += '-';
			--x;
			ioHsp.mGapped = true;
		}
	}
	
	qLen += static_cast<uint32>(qi - qis);
	sLen += static_cast<uint32>(si - sis);
	
	// the seed itself
	alignedQuery += mQuery[querySeed];
	alignedTarget += inTarget[targetSeed];
	score += mMatrix(mQuery[querySeed], inTarget[targetSeed]);

	// and the part after the seed
	M6DPData d2(mQuery.length() - querySeed, inTarget.length() - targetSeed);
	M6RecordTraceBack tba(d2);

	score += AlignGapped(
		mQuery.begin() + querySeed + 1, mQuery.end(),
		inTarget.begin() + targetSeed + 1, inTarget.end(),
		tba, mXgFinal, x, y);

	sequence::const_reverse_iterator qri = mQuery.rbegin() + (mQuery.length() - querySeed) - 1 - x, qris = qri;
	sequence::const_reverse_iterator sri = inTarget.rbegin() + (inTarget.length() - targetSeed) - 1 - y, sris = sri;
	
	sequence q, s;
	
	while (x >= 1 and y >= 1)
	{
		if (x >= 1 and y >= 1 and d2(x, y) == 0)
		{
			q += *qri++;
			s += *sri++;
			--x;
			--y;
		}
		else if (y >= 1 and d2(x, y) < 0)
		{
			q += '-';
			s += *sri++;
			--y;
			ioHsp.mGapped = true;
		}
		else // if (x >= 1 and d2(x, y) > 0)
		{
			q += *qri++;
			s += '-';
			--x;
			ioHsp.mGapped = true;
		}
	}

	reverse(q.begin(), q.end());
	reverse(s.begin(), s.end());
	
	alignedQuery += q;
	alignedTarget += s;
	
	qLen += static_cast<uint32>(qri - qris);
	sLen += static_cast<uint32>(sri - sris);
	
	ioHsp.mAlignedQuery.assign(alignedQuery.begin(), alignedQuery.end());
	ioHsp.mAlignedTarget.assign(alignedTarget.begin(), alignedTarget.end());
	ioHsp.mQueryEnd = ioHsp.mQueryStart + qLen;
	ioHsp.mTargetEnd = ioHsp.mTargetStart + sLen;
	
	return score;
}

template<int WORDSIZE>
void M6BlastQuery<WORDSIZE>::AddHit(M6Hit* inHit)
{
	sort(inHit->mHsps.begin(), inHit->mHsps.end(), greater<M6Hsp>());

	mHits.push_back(inHit);
	
	auto cmp = [](const M6Hit* a, const M6Hit* b) -> bool {
		return a->mHsps.front().mScore > b->mHsps.front().mScore;
	};
	
	push_heap(mHits.begin(), mHits.end(), cmp);
	if (mHits.size() > mReportLimit * 2)
	{
		pop_heap(mHits.begin(), mHits.end(), cmp);
		delete mHits.back();
		mHits.erase(mHits.end() - 1);
	}
}

// --------------------------------------------------------------------

Result* Search(const fs::path& inDatabank,
	const string& inQuery, const string& inProgram,
	const string& inMatrix, uint32 inWordSize, double inExpect,
	bool inFilter, bool inGapped, uint32 inGapOpen, uint32 inGapExtend,
	uint32 inReportLimit, uint32 inThreads)
{
	io::mapped_file file(inDatabank.string().c_str(), io::mapped_file::readonly);
	if (not file.is_open())
		throw M6Exception("FastA file %s not open", inDatabank.string().c_str());

	const char* data = file.const_data();
	size_t length = file.size();
	
	boost::timer::auto_cpu_timer t;

	if (inProgram != "blastp")
		throw M6Exception("Unsupported program %s", inProgram.c_str());
	
	if (inGapped)
	{
		if (inGapOpen == -1) inGapOpen = 11;
		if (inGapExtend == -1) inGapExtend = 1;
	}

	M6Matrix matrix(inMatrix, inGapOpen, inGapExtend);

	unique_ptr<Result> result(new Result);
	switch (inWordSize)
	{
		case 3:
		{
			M6BlastQuery<3> query(inQuery, inFilter, inExpect, inGapped, matrix, inReportLimit);
			query.Search(data, length);
			query.Report(*result);
			break;
		}

		default:
			throw M6Exception("Unsupported word size %d", inWordSize);	
	}
	
	return result.release();
}

}

// --------------------------------------------------------------------

ostream& operator<<(ostream& os, const M6Blast::Result& inResult)
{
	xml::writer w(os);
	w.xml_decl(true);
	w.start_element("BlastOutput");
//	w.element("BlastOutput_program", "blastp");
	
	w.start_element("BlastOutput_iterations");
	
	w.end_element();
	
	w.end_element();
	return os;
}

// --------------------------------------------------------------------

int main()
{
	try
	{
		const char kQuery[] = "MALLKVKFDQKKRVKLAQGLWLMNWLSVLAGIVIFSLGLFLKIELRKRSDVMNNSESHFVPNSLIVMGVLSCVFNSLAGKICYDALDPAKYAKWKPWLKPYLAVCVLFNIALFLVTLCCFLMRGSLESTLAHGLKNGMKYYRDTDTPGRCFMKKTIDMLQIEFRCCGNNGFRDWFEIQWISNRYLDFSSKEVKDRIKSNVDGRYLVDGVPFSCCNPSSPRPCIQYQLTNNSAHYSYDHQTEELNLWVNGCRAALLSYYSSLMNSMGAVTLLVWLFEVTITIGLRYLHTALEGVSNPEDPECESEGWLLEKSVSETWKAFLESLKKLGKSNQVEAEGADAGQAPEAG";
		
		M6Blast::Result* r = M6Blast::Search(fs::path("C:/data/fasta/uniprot_sprot.fasta"),
			kQuery, "blastp", "BLOSUM62", 3, 10.0, true, true, -1, -1, 250);
		
		if (r != nullptr)
			cout << *r << endl;
		else
			cout << "no hits found" << endl;

		delete r;
	}
	catch (exception& e)
	{
		cerr << e.what() << endl;
	}
	
	return 0;
}
