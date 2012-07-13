#pragma once

#include <queue>

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/tr1/tuple.hpp>

#include "M6Blast.h"

// --------------------------------------------------------------------

struct sqlite3;
struct sqlite3_stmt;

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

typedef std::shared_ptr<const M6Blast::Result> M6BlastResultPtr;

class M6BlastCache
{
  public:
	static M6BlastCache&		Instance();

	std::tr1::tuple<M6BlastJobStatus,std::string,uint32,double>
								JobStatus(const std::string& inJobID);

	M6BlastResultPtr			JobResult(const std::string& inJobID);
	
	std::string					Submit(const std::string& inDatabank,
									std::string inQuery, //std::string inProgram,
									std::string inMatrix, uint32 inWordSize,
									double inExpect, bool inLowComplexityFilter,
									bool inGapped, int32 inGapOpen, int32 inGapExtend,
									uint32 inReportLimit);

	void						Purge(bool inDeleteFiles = false);

  private:

								M6BlastCache();
								M6BlastCache(const M6BlastCache&);
	M6BlastCache&				operator=(const M6BlastCache&);
								~M6BlastCache();

	void						Work();
	void						ExecuteJob(const std::string& inJobID);
	void						SetJobStatus(const std::string inJobID, const std::string& inStatus,
									const std::string& inError, uint32 inHitCount, double inBestScore);
	void						CacheResult(const std::string& inJobID, M6BlastResultPtr inResult);

	void						ExecuteStatement(const std::string& inStatement);

	boost::filesystem::path		mCacheDir;
	sqlite3*					mCacheDB;
	sqlite3_stmt*				mSelectByParamsStmt;
	sqlite3_stmt*				mSelectByStatusStmt;
	sqlite3_stmt*				mSelectByIDStmt;
	sqlite3_stmt*				mInsertStmt;
	sqlite3_stmt*				mUpdateStatusStmt;
	sqlite3_stmt*				mFetchParamsStmt;
	boost::thread				mWorkerThread;
	boost::mutex				mDbMutex, mCacheMutex;
	boost::condition			mEmptyCondition, mWorkCondition;
	
	struct Cached
	{
		std::string				id;
		M6BlastResultPtr		result;
	};
	
	std::list<Cached>			mResultCache;
};
