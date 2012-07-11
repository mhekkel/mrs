#pragma once

#include <boost/uuid/uuid.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/filesystem/path.hpp>

#include "M6Blast.h"

// --------------------------------------------------------------------

struct sqlite3;

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

class M6BlastCache
{
  public:
	static M6BlastCache&		Instance();

	M6BlastJobStatus			JobStatus(const boost::uuids::uuid& inJobID);
	std::string					JobError(const boost::uuids::uuid& inJobID);
	const M6Blast::Result*		JobResult(const boost::uuids::uuid& inJobID);
	
	boost::uuids::uuid			Submit(const std::string& inDatabank,
									std::string inQuery, //std::string inProgram,
									std::string inMatrix, uint32 inWordSize,
									double inExpect, bool inLowComplexityFilter,
									bool inGapped, uint32 inGapOpen, uint32 inGapExtend,
									uint32 inReportLimit);

  private:

								M6BlastCache();
								M6BlastCache(const M6BlastCache&);
	M6BlastCache&				operator=(const M6BlastCache&);
								~M6BlastCache();

	void						Work();
	void						ExecuteJob(const boost::uuids::uuid& inJobID);

	void						ExecuteStatement(const std::string& inStatement);

//	void						SelectOne(R& result, const std::string& statement,
//	template<class R, class A0, class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8>
//									const A0& a0, const A1& a1, const A2& a2,
//									const A3& a3, const A4& a4, const A5& a5,
//									const A6& a6, const A7& a7, const A8& a8);

	boost::filesystem::path		mCacheDir;
	sqlite3*					mCacheDB;
	boost::thread				mWorkerThread;
	boost::mutex				mDbMutex;
	boost::condition			mEmptyCondition;
};
