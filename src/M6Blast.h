//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <string>
#include <list>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <zeep/xml/serialize.hpp>

namespace M6Blast
{

struct Hsp
{
	uint32			mHspNr;
	uint32			mQueryStart;
	uint32			mQueryEnd;
	uint32			mTargetStart;
	uint32			mTargetEnd;
	uint32			mTargetLength;
	uint32			mScore;
	double			mBitScore;
	double			mExpect;
	uint32			mIdentity;
	uint32			mPositive;
	uint32			mGaps;
	std::string		mQueryAlignment;
	std::string		mTargetAlignment;
	std::string		mMidLine;
	
	template<class Archive>
	void			serialize(Archive& ar, const unsigned int version)
					{
						ar & boost::serialization::make_nvp("nr", mHspNr)
						   & boost::serialization::make_nvp("q-start", mQueryStart)
						   & boost::serialization::make_nvp("q-end", mQueryEnd)
						   & boost::serialization::make_nvp("t-start", mTargetStart)
						   & boost::serialization::make_nvp("t-end", mTargetEnd)
						   & boost::serialization::make_nvp("t-len", mTargetLength)
						   & boost::serialization::make_nvp("score", mScore)
						   & boost::serialization::make_nvp("bit-score", mBitScore)
						   & boost::serialization::make_nvp("expect", mExpect)
						   & boost::serialization::make_nvp("identity", mIdentity)
						   & boost::serialization::make_nvp("positive", mPositive)
						   & boost::serialization::make_nvp("gaps", mGaps)
						   & boost::serialization::make_nvp("q-align", mQueryAlignment)
						   & boost::serialization::make_nvp("t-align", mTargetAlignment)
						   & boost::serialization::make_nvp("midline", mMidLine)
						   ;
					}
};

struct Hit
{
	uint32			mHitNr;
	std::string		mDb;
	std::string		mID;
	std::string		mChain;
	std::string		mTitle;
	std::string		mDefLine;
	std::string		mSequence;
	std::list<Hsp>	mHsps;

	template<class Archive>
	void			serialize(Archive& ar, const unsigned int version)
					{
						ar & boost::serialization::make_nvp("nr", mHitNr)
						   & boost::serialization::make_nvp("db", mDb)
						   & boost::serialization::make_nvp("id", mID)
						   //& boost::serialization::make_nvp("acc", mAccession)
						   & boost::serialization::make_nvp("chain", mChain)
						   & boost::serialization::make_nvp("def", mDefLine)
						   & boost::serialization::make_nvp("title", mTitle)
						   & boost::serialization::make_nvp("seq", mSequence)
						   & boost::serialization::make_nvp("hsp", mHsps)
						   ;
					}
};

struct Parameters
{
	// parameters
	std::string		mProgram;
	std::string		mMatrix;
	double			mExpect;
	bool			mGapped;
	int32			mGapOpen;
	int32			mGapExtend;
	bool			mFilter;

	template<class Archive>
	void			serialize(Archive& ar, const unsigned int version)
					{
						ar & boost::serialization::make_nvp("program", mProgram)
						   & boost::serialization::make_nvp("matrix", mMatrix)
						   & boost::serialization::make_nvp("expect", mExpect)
						   & boost::serialization::make_nvp("gapped", mGapped)
						   & boost::serialization::make_nvp("gap-open", mGapOpen)
						   & boost::serialization::make_nvp("gap-extend", mGapExtend)
						   & boost::serialization::make_nvp("filter", mFilter)
						   ;
					}
};

struct Stats
{
	// stats
	uint32			mDbCount;
	uint64			mDbLength;
	uint64			mEffectiveSpace;
	double			mKappa;
	double			mLambda;
	double			mEntropy;

	template<class Archive>
	void			serialize(Archive& ar, const unsigned int version)
					{
						ar & boost::serialization::make_nvp("db-count", mDbCount)
						   & boost::serialization::make_nvp("db-length", mDbLength)
						   & boost::serialization::make_nvp("effective-space", mEffectiveSpace)
						   & boost::serialization::make_nvp("kappa", mKappa)
						   & boost::serialization::make_nvp("lambda", mLambda)
						   & boost::serialization::make_nvp("entropy", mEntropy)
						   ;
					}
};

struct Result
{
	Parameters		mParams;
	std::string		mDb;
	std::string		mQueryID;
	std::string		mQueryDef;
	uint32			mQueryLength;
	std::list<Hit>	mHits;
	Stats			mStats;

	template<class Archive>
	void			serialize(Archive& ar, const unsigned int version)
					{
						ar & boost::serialization::make_nvp("params", mParams)
						   & boost::serialization::make_nvp("db", mDb)
						   & boost::serialization::make_nvp("query-id", mQueryID)
						   & boost::serialization::make_nvp("query-def", mQueryDef)
						   & boost::serialization::make_nvp("query-length", mQueryLength)
						   & boost::serialization::make_nvp("hit", mHits)
						   & boost::serialization::make_nvp("stats", mStats)
						   ;
					}
	
	void			WriteAsNCBIBlastXML(std::ostream& os);
};

Result* Search(const std::vector<boost::filesystem::path>& inDatabanks,
	const std::string& inQuery, const std::string& inProgram,
	const std::string& inMatrix, uint32 inWordSize, double inExpect,
	bool inFilter, bool inGapped, int32 inGapOpen, int32 inGapExtend,
	uint32 inReportLimit, uint32 inThreads = 0);

}

