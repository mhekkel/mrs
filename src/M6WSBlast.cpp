//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <set>
#include <boost/regex.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/operations.hpp>

#include "M6Config.h"
#include "M6Error.h"
#include "M6BlastCache.h"
#include "M6Databank.h"

#include "M6WSBlast.h"
#include "M6Server.h"

using namespace std;
namespace zx = zeep::xml;
namespace ba = boost::algorithm;
namespace fs = boost::filesystem;

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

Parameters::Parameters(boost::optional<Parameters>& rhs)
	: matrix("BLOSUM62")
	, wordSize(3)
	, expect(10)
	, lowComplexityFilter(true)
	, gapped(true)
	, gapOpen(11)
	, gapExtend(1)
{
	if (rhs)
	{
		matrix =				boost::get_optional_value_or(rhs.get().matrix, "BLOSUM62");
		wordSize =				boost::get_optional_value_or(rhs.get().wordSize, 3);
		expect =				boost::get_optional_value_or(rhs.get().expect, 10);
		lowComplexityFilter =	boost::get_optional_value_or(rhs.get().lowComplexityFilter, true);
		gapped =				boost::get_optional_value_or(rhs.get().gapped, true);
		gapOpen =				boost::get_optional_value_or(rhs.get().gapOpen, 11);
		gapExtend =				boost::get_optional_value_or(rhs.get().gapExtend, 1);
	}
}
	
}

// --------------------------------------------------------------------
//
//	Server implementation
// 

M6WSBlast::M6WSBlast(M6Server& inServer, const string& inNS, const string& inService)
	: zeep::dispatcher(inNS, inService)
	, mServer(inServer)
{
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

	register_action("Blast", this, &M6WSBlast::Blast, {"query", "program", "db", "params", "reportLimit", "jobId"});
	register_action("BlastJobStatus", this, &M6WSBlast::BlastJobStatus, {"jobId", "status"});
	register_action("BlastJobResult", this, &M6WSBlast::BlastJobResult, {"jobId", "result"});
	register_action("BlastJobError", this, &M6WSBlast::BlastJobError, {"jobId", "error"});
}

M6WSBlast::~M6WSBlast()
{
}

void M6WSBlast::Blast(const string& query, const string& program, const string& db,
	boost::optional<M6WSBlastNS::Parameters> params,
	boost::optional<uint32> reportLimit, string& response)
{
	// check the program parameter
	if (program != "blastp")
		THROW(("Only blastp is supported for now, sorry"));

	// see if we can blast this databank
	vector<string> dbs(mServer.UnAlias(db));
	if (dbs.empty())
		THROW(("Databank '%s' not configured", db.c_str()));

	for (string adb : dbs)
	{
		M6Databank* mdb = mServer.Load(adb);
		if (mdb == nullptr)
			THROW(("Databank '%s' not configured", adb.c_str()));
		if (not fs::exists(mdb->GetDbDirectory() / "fasta"))
			THROW(("Databank does not have blastable sequences (%s/%s)", db.c_str(), adb.c_str()));
	}
	
//	// try to load the matrix, fails if the parameters are incorrect
//	M6Matrix matrix(params.matrix, params.gapOpen, params.gapExtend);

	M6WSBlastNS::Parameters p(params);
	
	response = M6BlastCache::Instance().Submit(
		ba::join(dbs, ";"), query, program,
		p.matrix.get(),
		p.wordSize.get(),
		p.expect.get(),
		p.lowComplexityFilter.get(),
		p.gapped.get(),
		p.gapOpen.get(),
		p.gapExtend.get(),
		reportLimit ? reportLimit.get() : 100);
}

void M6WSBlast::BlastJobStatus(string job_id, M6WSBlastNS::JobStatus& response)
{
	M6BlastJobStatus status;
	string error;
	uint32 hitCount;
	double bestScore;
	
	tie(status, error, hitCount, bestScore) = M6BlastCache::Instance().JobStatus(job_id);
	
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
	// check status first
	M6BlastJobStatus status;
	string error;
	uint32 hitCount;
	double bestScore;
	
	tie(status, error, hitCount, bestScore) = M6BlastCache::Instance().JobStatus(job_id);
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
	
	for (const M6Blast::Hit& hit : hits)
	{
		const list<M6Blast::Hsp>& hsps(hit.mHsps);
		if (hsps.empty())
			continue;

		M6WSBlastNS::Hit h;
		
		h.id = hit.mID;
		h.title = hit.mDefLine;
		if (not hit.mChain.empty())
			h.sequenceId.reset(hit.mChain);
		
		for (const M6Blast::Hsp& hsp : hsps)
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
	M6BlastJobStatus status;
	uint32 hitCount;
	double bestScore;
	
	tie(status, response, hitCount, bestScore) = M6BlastCache::Instance().JobStatus(job_id);
}
