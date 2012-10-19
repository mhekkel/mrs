#include "M6Lib.h"

#include <set>
#include <boost/regex.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Config.h"
#include "M6Error.h"
#include "M6BlastCache.h"

#include "M6WSBlast.h"

using namespace std;
namespace zx = zeep::xml;

const
	string	kBlastNS = "http://mrs.cmbi.ru.nl/mrsws/blast";

namespace M6WSBlastNS
{

Parameters::Parameters()
	: matrix("BLOSUM62")
	, wordSize(3)
	, expect(10)
	, lowComplexityFilter(true)
	, gapped(true)
	, gapOpen(11)
	, gapExtend(1)
{
}
	
}

// --------------------------------------------------------------------
//
//	Server implementation
// 

M6WSBlast::M6WSBlast(const zeep::xml::element* inConfig)
	: zeep::server(kBlastNS, "mrsws_blast")
{
	zx::element* addr = inConfig->find_first("external-address");
	if (addr != nullptr)
		set_location(addr->content());

	foreach (zx::element* db, inConfig->find("dbs/db"))
		mDbTable.push_back(db->content());

	using namespace M6WSBlastNS;
	
	SOAP_XML_SET_STRUCT_NAME(Parameters);
	SOAP_XML_SET_STRUCT_NAME(Hsp);
	SOAP_XML_SET_STRUCT_NAME(Hit);
	SOAP_XML_SET_STRUCT_NAME(BlastResult);
	
	SOAP_XML_ADD_ENUM(JobStatus, unknown);
	SOAP_XML_ADD_ENUM(JobStatus, queued);
	SOAP_XML_ADD_ENUM(JobStatus, running);
	SOAP_XML_ADD_ENUM(JobStatus, error);
	SOAP_XML_ADD_ENUM(JobStatus, finished);
	
	const char* kBlastArgs[] = {
		"query", "program", "db", "mrsBooleanQuery", "params", "reportLimit", "jobId"
	};
	register_action("Blast", this, &M6WSBlast::Blast, kBlastArgs);
		
	const char* kBlastJobStatusArgs[] = {
		"jobId", "status"
	};
	register_action("BlastJobStatus", this, &M6WSBlast::BlastJobStatus, kBlastJobStatusArgs);
		
	const char* kBlastJobResultArgs[] = {
		"jobId", "result"
	};
	register_action("BlastJobResult", this, &M6WSBlast::BlastJobResult, kBlastJobResultArgs);
	
	const char* kBlastJobErrorArgs[] = {
		"jobId", "error"
	};
	register_action("BlastJobError", this, &M6WSBlast::BlastJobError, kBlastJobErrorArgs);
}

M6WSBlast::~M6WSBlast()
{
}

void M6WSBlast::Blast(const string& query, const string& program, const string& db,
	const string& mrsBooleanQuery, const M6WSBlastNS::Parameters& params,
	uint32 reportLimit, string& response)
{
	// check the program parameter
	if (program != "blastp")
		THROW(("Only blastp is supported for now, sorry"));

	// see if we can blast this databank
	if (find(mDbTable.begin(), mDbTable.end(), db) == mDbTable.end())
		THROW(("Databank %s cannot be used to do blast searches", db.c_str()));

//	// try to load the matrix, fails if the parameters are incorrect
//	M6Matrix matrix(params.matrix, params.gapOpen, params.gapExtend);

	response = M6BlastCache::Instance().Submit(
		db, query, params.matrix, params.wordSize,
		params.expect, params.lowComplexityFilter,
		params.gapped, params.gapOpen, params.gapExtend, reportLimit);

	log() << response;
}

void M6WSBlast::BlastJobStatus(string job_id, M6WSBlastNS::JobStatus& response)
{
	log() << job_id;

	M6BlastJobStatus status;
	string error;
	uint32 hitCount;
	double bestScore;
	
	tr1::tie(status, error, hitCount, bestScore) = M6BlastCache::Instance().JobStatus(job_id);
	
	switch (status)
	{
		case bj_Unknown:	response = M6WSBlastNS::unknown; break;
		case bj_Queued:		response = M6WSBlastNS::queued; break;
		case bj_Running:	response = M6WSBlastNS::running; break;
		case bj_Finished:	response = M6WSBlastNS::finished; break;
		case bj_Error:		response = M6WSBlastNS::error; break;
	}
}

void M6WSBlast::BlastJobResult(string job_id, M6WSBlastNS::BlastResult& response)
{
	log() << job_id;

	// check status first
	M6BlastJobStatus status;
	string error;
	uint32 hitCount;
	double bestScore;
	
	tr1::tie(status, error, hitCount, bestScore) = M6BlastCache::Instance().JobStatus(job_id);
	if (status != bj_Finished)
		THROW(("Job %s not finished yet", job_id.c_str()));
	
	M6BlastResultPtr result = M6BlastCache::Instance().JobResult(job_id);
	
	response.dbCount = result->mStats.mDbCount;
	response.dbLength = result->mStats.mDbLength;
	response.effectiveSearchSpace = result->mStats.mEffectiveSpace;
	response.kappa = result->mStats.mKappa;
	response.lambda = result->mStats.mLambda;
	response.entropy = result->mStats.mEntropy;

	const list<M6Blast::Hit>& hits(result->mHits);
	
	foreach (const M6Blast::Hit& hit, hits)
	{
		const list<M6Blast::Hsp>& hsps(hit.mHsps);
		if (hsps.empty())
			continue;

		M6WSBlastNS::Hit h;
		
		h.id = hit.mID;
		h.title = hit.mDefLine;
		
//		string sequenceId = hit.sequenceID;
//		if (not sequenceId.empty())
//			h.sequenceId.push_back(sequenceId);
		
		foreach (const M6Blast::Hsp& hsp, hsps)
		{
			M6WSBlastNS::Hsp hs;
			
			hs.score = hsp.mScore;
			hs.bitScore = hsp.mBitScore;
			hs.expect = hsp.mExpect;
			hs.queryStart = hsp.mQueryStart;
			hs.subjectStart = hsp.mTargetStart;
			hs.identity = hsp.mIdentity;
			hs.positive = hsp.mPositive;
			hs.gaps = hsp.mGaps;
			hs.subjectLength = hsp.mTargetLength;
			hs.queryAlignment = hsp.mQueryAlignment;
			hs.subjectAlignment = hsp.mTargetAlignment;
			hs.midline = hsp.mMidLine;
			
			h.hsps.push_back(hs);
		}
		
		response.hits.push_back(h);
	}
}

void M6WSBlast::BlastJobError(string job_id, string& response)
{
	log() << job_id;

	M6BlastJobStatus status;
	uint32 hitCount;
	double bestScore;
	
	tr1::tie(status, response, hitCount, bestScore) = M6BlastCache::Instance().JobStatus(job_id);
}
