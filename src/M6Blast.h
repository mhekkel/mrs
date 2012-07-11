#pragma once

#include <string>
#include <list>
#include <vector>

#include <boost/filesystem/path.hpp>

namespace M6Blast
{

struct Hsp
{
	uint32			mHspNr;
	uint32			mQueryStart;
	uint32			mQueryEnd;
	uint32			mTargetStart;
	uint32			mTargetEnd;
	uint32			mScore;
	double			mBitScore;
	double			mExpect;
	uint32			mIdentity;
	uint32			mPositive;
	uint32			mGaps;
	std::string		mQueryAlignment;
	std::string		mTargetAlignment;
	std::string		mMidLine;
};

struct Hit
{
	uint32			mHitNr;
	std::string		mID;
	std::string		mAccession;
	std::string		mDefLine;
	std::string		mSequence;
	std::list<Hsp>	mHsps;
};

struct Result
{
	std::string		mProgram;
	std::string		mDb;
	std::string		mQueryID;
	std::string		mQueryDef;
	uint32			mQueryLength;
	std::list<Hit>	mHits;

	// parameters
	std::string		mMatrix;
	double			mExpect;
	int32			mGapOpen;
	int32			mGapExtend;
	bool			mFilter;

	// stats
	uint32			mDbCount;
	uint64			mDbLength;
	uint64			mEffectiveSpace;
	double			mKappa;
	double			mLambda;
	double			mEntropy;
};

Result* Search(const std::vector<boost::filesystem::path>& inDatabanks,
	const std::string& inQuery, const std::string& inProgram,
	const std::string& inMatrix, uint32 inWordSize, double inExpect,
	bool inFilter, bool inGapped, int32 inGapOpen, int32 inGapExtend,
	uint32 inReportLimit, uint32 inThreads = 0);

void SearchAndWriteResultsAsFastA(std::ostream& inOutFile,
	const std::vector<boost::filesystem::path>& inDatabanks,
	const std::string& inQuery, const std::string& inProgram,
	const std::string& inMatrix, uint32 inWordSize, double inExpect,
	bool inFilter, bool inGapped, int32 inGapOpen, int32 inGapExtend,
	uint32 inReportLimit, uint32 inThreads = 0);

}

std::ostream& operator<<(std::ostream& os, const M6Blast::Result& inResult);

