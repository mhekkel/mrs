#pragma once

#include <string>
#include <vector>

namespace M6Blast
{

struct Hsp
{
	uint32				mQueryStart;
	uint32				mQueryEnd;
	uint32				mSubjectStart;
	uint32				mSubjectEnd;
	uint32				mSubjectLength;
	uint32				mScore;
	double				mBitScore;
	double				mExpect;
	uint32				mIdentity;
	uint32				mPositive;
	uint32				mGaps;
	std::string			mQueryAlignment;
	std::string			mSubjectAlignment;
	std::string			mMidLine;
};

struct Hit
{
	std::string			mID;
	std::string			mAccession;
	std::string			mDefLine;
	std::list<Hsp>		mHsps;
};

struct Result
{
	uint32			mDbCount;
	uint64			mDbLength;
	uint64			mEffectiveSpace;
	double			mKappa;
	double			mLambda;
	double			mEntropy;
	std::list<Hit>	mHits;
};

Result* Search(const boost::filesystem::path& inDatabank,
	const std::string& inQuery, const std::string& inProgram,
	const std::string& inMatrix, uint32 inWordSize, double inExpect,
	bool inFilter, bool inGapped, uint32 inGapOpen, uint32 inGapExtend,
	uint32 inReportLimit, uint32 inThreads = 0);

}

std::ostream& operator<<(std::ostream& os, const M6Blast::Result& inResult);

