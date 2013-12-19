//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <queue>

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/tr1/tuple.hpp>

#include "M6Blast.h"

// --------------------------------------------------------------------

enum M6BlastJobStatus
{
	bj_Unknown,
	bj_Queued,
	bj_Running,
	bj_Finished,
	bj_Error
};

// --------------------------------------------------------------------

struct M6BlastDbInfo
{
	std::string					path;
	boost::posix_time::ptime	timestamp;
	
	bool operator==(const M6BlastDbInfo& rhs) const	{ return path == rhs.path and timestamp == rhs.timestamp; }

	template<class Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & BOOST_SERIALIZATION_NVP(path) & BOOST_SERIALIZATION_NVP(timestamp);
	}
};

struct M6BlastJob
{
	// actual parameters
	std::string					db;
	std::string					query;
	std::string					program;
	std::string					matrix;
	uint32						wordsize;
	double						expect;
	bool						filter;
	bool						gapped;
	int32						gapOpen;
	int32						gapExtend;
	uint32						reportLimit;
	
	// administrative info
	std::vector<M6BlastDbInfo>	files;
	
	// convenience
	bool						IsJobFor(const std::string& inDatabank,
									const std::string& inQuery, const std::string& inProgram,
									const std::string& inMatrix, uint32 inWordSize,
									double inExpect, bool inLowComplexityFilter,
									bool inGapped, int32 inGapOpen, int32 inGapExtend,
									uint32 inReportLimit) const;

	bool						IsStillValid(const std::vector<boost::filesystem::path>& inFiles) const;
	
	template<class Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & BOOST_SERIALIZATION_NVP(db)
		   & BOOST_SERIALIZATION_NVP(query)
		   & BOOST_SERIALIZATION_NVP(program)
		   & BOOST_SERIALIZATION_NVP(matrix)
		   & BOOST_SERIALIZATION_NVP(wordsize)
		   & BOOST_SERIALIZATION_NVP(expect)
		   & BOOST_SERIALIZATION_NVP(filter)
		   & BOOST_SERIALIZATION_NVP(gapped)
		   & BOOST_SERIALIZATION_NVP(gapOpen)
		   & BOOST_SERIALIZATION_NVP(gapExtend)
		   & BOOST_SERIALIZATION_NVP(reportLimit)
		   & BOOST_SERIALIZATION_NVP(files);
	}
};

// --------------------------------------------------------------------

typedef std::shared_ptr<const M6Blast::Result> M6BlastResultPtr;

struct M6BlastJobDesc
{
	std::string		id;
	std::string		db;
	uint32			queryLength;
	std::string		status;
};

typedef std::list<M6BlastJobDesc> M6BlastJobDescList;

class M6BlastCache
{
  public:
	static M6BlastCache&		Instance();

	std::tr1::tuple<M6BlastJobStatus,std::string,uint32,double>
								JobStatus(const std::string& inJobID);

	M6BlastResultPtr			JobResult(const std::string& inJobID);
	
	std::string					Submit(const std::string& inDatabank,
									const std::string& inQuery, const std::string& inProgram,
									const std::string& inMatrix, uint32 inWordSize,
									double inExpect, bool inLowComplexityFilter,
									bool inGapped, int32 inGapOpen, int32 inGapExtend,
									uint32 inReportLimit);

	void						Purge(bool inDeleteFiles = false);

	M6BlastJobDescList			GetJobList();
	void						DeleteJob(const std::string& inJobID);

  private:

								M6BlastCache();
								M6BlastCache(const M6BlastCache&);
	M6BlastCache&				operator=(const M6BlastCache&);
								~M6BlastCache();

	void						Work();
	void						ExecuteJob(const std::string& inJobID);
	void						StoreJob(const std::string& inJobID, const M6BlastJob& inJob);
	void						SetJobStatus(const std::string inJobId, M6BlastJobStatus inStatus);
	void						CacheResult(const std::string& inJobId, M6BlastResultPtr inResult);

	void						FastaFilesForDatabank(const std::string& inDatabank,
									std::vector<boost::filesystem::path>& outFiles);

	void						ExecuteStatement(const std::string& inStatement);

	boost::filesystem::path		mCacheDir;
	boost::thread				mWorkerThread;
	boost::mutex				mCacheMutex, mWorkMutex;
	boost::condition			mWorkCondition;
	bool						mStopWorkingFlag;
	
	struct CacheEntry
	{
		std::string				id;
		M6BlastJob				job;
		M6BlastJobStatus		status;
		uint32					hitCount;
		double					bestScore;
	};
	
	std::list<CacheEntry>		mResultCache;
};
