#include "M6Lib.h"

#include <iostream>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/current_function.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/format.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string.hpp>

#include <sqlite3.h>

#include "M6Config.h"
#include "M6Error.h"
#include "M6BlastCache.h"

using namespace std;

namespace fs = boost::filesystem;
namespace io = boost::iostreams;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

void ThrowDbException(sqlite3* conn, int err, const char* stmt, const char* file, int line, const char* func)
{
	const char* errmsg = sqlite3_errmsg(conn);
	cerr << "sqlite3 error '" << errmsg << "' in " << file << ':' << line << ": " << func << endl;
	throw M6Exception(errmsg);
}

#define THROW_IF_SQLITE3_ERROR(e,conn) \
	do { int _e(e); if (_e != SQLITE_OK and _e != SQLITE_DONE) ThrowDbException(conn, e, #e, __FILE__, __LINE__, BOOST_CURRENT_FUNCTION); } while (false)

// --------------------------------------------------------------------

M6BlastJobStatus ParseStatus(const string& inStatus)
{
	M6BlastJobStatus result = bj_Unknown;
	if (inStatus == "queued")			result = bj_Queued;
	else if (inStatus == "running")		result = bj_Running;
	else if (inStatus == "finished")	result = bj_Finished;
	else if (inStatus == "error")		result = bj_Error;
	return result;
}

// --------------------------------------------------------------------

M6BlastCache& M6BlastCache::Instance()
{
	static M6BlastCache sInstance;
	return sInstance;
}

M6BlastCache::M6BlastCache()
	: mCacheDB(nullptr)
	, mSelectByParamsStmt(nullptr)
	, mSelectByStatusStmt(nullptr)
	, mSelectByIDStmt(nullptr)
	, mInsertStmt(nullptr)
	, mUpdateStatusStmt(nullptr)
	, mFetchParamsStmt(nullptr)
{
	boost::mutex::scoped_lock lock(mDbMutex);
	
	string s = M6Config::Instance().FindGlobal("/m6-config/blastdir");
	if (s.empty())
		THROW(("Missing blastdir configuration"));

	mCacheDir = fs::path(s);
	if (not fs::exists(mCacheDir))
		fs::create_directory(mCacheDir);
	
	s = (mCacheDir / "index.db").string();

	sqlite3_config(SQLITE_CONFIG_MULTITHREAD);

	THROW_IF_SQLITE3_ERROR(sqlite3_open_v2(s.c_str(), &mCacheDB,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr), nullptr);

	sqlite3_extended_result_codes(mCacheDB, true);
	
	ExecuteStatement(
		"CREATE TABLE IF NOT EXISTS blast_cache ( "
			"id TEXT, "
			"status TEXT, "
			"error TEXT, "
			"hitcount INTEGER, "
			"bestscore REAL, "
			"db TEXT, "
			"query TEXT, "
			"matrix TEXT, "
			"wordsize INTEGER, "
			"expect REAL, "
			"filter INTEGER, "
			"gapped INTEGER, "
			"gapopen INTEGER, "
			"gapextend INTEGER, "
			"UNIQUE(id)"
		")"
	);
	
	ExecuteStatement(
		"CREATE TABLE IF NOT EXISTS blast_db_file ( "
			"db TEXT, "
			"file TEXT, "
			"time_t INTEGER"
		")"
	);

//	ExecuteStatement("DELETE FROM blast_cache");
	ExecuteStatement("DELETE FROM blast_cache WHERE status = 'running' ");

	THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
		"SELECT status, error, hitcount, bestscore FROM blast_cache WHERE id = ? ", -1,
		&mSelectByIDStmt, nullptr), mCacheDB);

	THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
		"SELECT id FROM blast_cache WHERE "
		"db = ? AND query = ? AND matrix = ? AND wordsize = ? AND expect = ? AND filter = ? AND gapped = ? AND gapopen = ? AND gapextend = ? ", -1,
		&mSelectByParamsStmt, nullptr), mCacheDB);

	THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
		"INSERT INTO blast_cache (id, status, db, query, matrix, wordsize, expect, filter, gapped, gapopen, gapextend) "
		"VALUES (?, 'queued', ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1,
		&mInsertStmt, nullptr), mCacheDB);

	THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
		"SELECT id FROM blast_cache WHERE status = 'queued' ", -1,
		&mSelectByStatusStmt, nullptr), mCacheDB);

	THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
		"UPDATE blast_cache SET status = ?, error = ?, hitcount = ?, bestscore = ? WHERE id = ? ", -1,
		&mUpdateStatusStmt, nullptr), mCacheDB);

	THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
		"SELECT db, query, matrix, wordsize, expect, filter, gapped, gapopen, gapextend FROM blast_cache WHERE id = ? ", -1,
		&mFetchParamsStmt, nullptr), mCacheDB);

	THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
		"SELECT file, time_t FROM blast_db_file WHERE db = ? ", -1,
		&mFetchDbFilesStmt, nullptr), mCacheDB);

	mWorkerThread = boost::thread([this](){ this->Work(); });
}

M6BlastCache::~M6BlastCache()
{
	if (mWorkerThread.joinable())
	{
		mWorkerThread.interrupt();
		mWorkerThread.join();
	}

	if (mSelectByParamsStmt != nullptr) sqlite3_finalize(mSelectByParamsStmt);
	if (mSelectByStatusStmt != nullptr) sqlite3_finalize(mSelectByStatusStmt);
	if (mSelectByIDStmt != nullptr) 	sqlite3_finalize(mSelectByIDStmt);
	if (mInsertStmt != nullptr) 		sqlite3_finalize(mInsertStmt);
	if (mUpdateStatusStmt != nullptr) 	sqlite3_finalize(mUpdateStatusStmt);
	if (mFetchParamsStmt != nullptr) 	sqlite3_finalize(mFetchParamsStmt);

	sqlite3_close(mCacheDB);
}

void M6BlastCache::ExecuteStatement(const string& stmt)
{
	if (VERBOSE)
		cout << stmt << endl;

	char* errmsg = NULL;
	int err = sqlite3_exec(mCacheDB, stmt.c_str(), nullptr, nullptr, &errmsg);
	if (errmsg != NULL)
	{
		cerr << errmsg << endl;
		sqlite3_free(errmsg);
	}
	
	THROW_IF_SQLITE3_ERROR(err, mCacheDB);
}

tr1::tuple<M6BlastJobStatus,string,uint32,double> M6BlastCache::JobStatus(const string& inJobID)
{
	boost::mutex::scoped_lock lock(mDbMutex);

	tr1::tuple<M6BlastJobStatus,string,uint32,double> result;
	
	string id = boost::lexical_cast<string>(inJobID);

	sqlite3_reset(mSelectByIDStmt);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mSelectByIDStmt, 1, id.c_str(), id.length(), SQLITE_STATIC), mCacheDB);

	int err = sqlite3_step(mSelectByIDStmt);
	if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
		THROW_IF_SQLITE3_ERROR(err, mCacheDB);

	if (err == SQLITE_ROW)
	{
		const char* text = reinterpret_cast<const char*>(sqlite3_column_text(mSelectByIDStmt, 0));
		uint32 length = sqlite3_column_bytes(mSelectByIDStmt, 0);
		string status(text, length);

		text = reinterpret_cast<const char*>(sqlite3_column_text(mSelectByIDStmt, 1));
		length = sqlite3_column_bytes(mSelectByIDStmt, 1);
		string error(text, length);

		result = tr1::make_tuple(ParseStatus(status), error, sqlite3_column_int(mSelectByIDStmt, 2), sqlite3_column_double(mSelectByIDStmt, 3));
	}
	else
		result = tr1::make_tuple(bj_Unknown, "", 0, 0);

	return result;
}

M6BlastResultPtr M6BlastCache::JobResult(const string& inJobID)
{
	M6BlastResultPtr result;

	{	
		boost::mutex::scoped_lock lock(mCacheMutex);

		foreach (auto& c, mResultCache)
		{
			if (c.id == inJobID)
			{
				result = c.result;
				break;
			}
		}
	}
	
	if (not result)
	{
		io::filtering_stream<io::input> in;
		fs::ifstream file(mCacheDir / (inJobID + ".xml.bz2"), ios::binary);
		if (not file.is_open())
			throw M6Exception("missing blast result file");

		in.push(io::bzip2_decompressor());
		in.push(file);
		
		zeep::xml::document doc(in);
		zeep::xml::deserializer d(doc.root());
	
		M6Blast::Result* r = new M6Blast::Result;
		result.reset(r);
		d & boost::serialization::make_nvp("blast-result", *r);
		
		CacheResult(inJobID, result);
	}

	return result;
}

void M6BlastCache::FastaFilesForDatabank(const string& inDatabank, vector<fs::path>& outFiles)
{
	fs::path mrsdir(M6Config::Instance().FindGlobal("/m6-config/mrsdir"));
	boost::format xp("/m6-config/databank[@id='%1%']/file");
	M6Config& config = M6Config::Instance();

	vector<string> dbs;
	ba::split(dbs, inDatabank, ba::is_any_of(";"));
	
	foreach (string& db, dbs)
	{
		fs::path dbdir(config.FindGlobal((xp % db).str()));
		if (dbdir.empty())
			THROW(("Databank directory not found for %s", db.c_str()));
		
		if (not dbdir.has_root_path())
			dbdir = mrsdir / dbdir;
		
		if (not fs::exists(dbdir / "fasta"))
			THROW(("Databank '%s' does not contain a fasta file", db.c_str()));
		
		outFiles.push_back(dbdir / "fasta");
	}
	
	if (outFiles.empty())
		THROW(("Databank '%s' does not contain a fasta file", inDatabank.c_str()));
}

void M6BlastCache::CheckCacheForDB(const string& inDatabank, const vector<fs::path>& inFiles)
{
	sqlite3_reset(mFetchDbFilesStmt);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mFetchDbFilesStmt, 1, inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mCacheDB);

	uint32 n = inFiles.size();
	bool clean = false;

	while (clean == false and sqlite3_step(mFetchDbFilesStmt) == SQLITE_ROW)
	{
		const char* text = reinterpret_cast<const char*>(sqlite3_column_text(mFetchDbFilesStmt, 0));
		uint32 length = sqlite3_column_bytes(mFetchDbFilesStmt, 0);
		string file(text, length);
		
		time_t timestamp = sqlite3_column_int64(mFetchDbFilesStmt, 1);

		clean = not fs::exists(file) or fs::last_write_time(file) != timestamp;
		--n;
	}

	if (clean or n > 0)
	{
		sqlite3_stmt* selectStmt = nullptr;
		sqlite3_stmt* deleteStmt = nullptr;
		sqlite3_stmt* updateStmt = nullptr;
		
		try
		{
			THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
				"SELECT id FROM blast_cache WHERE db = ? ", -1,
				&selectStmt, nullptr), mCacheDB);

			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(selectStmt, 1,
				inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mCacheDB);
			
			while (sqlite3_step(selectStmt) == SQLITE_ROW)
			{
				const char* text = reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 0));
				uint32 length = sqlite3_column_bytes(selectStmt, 0);

				fs::path file = mCacheDir / (string(text, length) + ".xml.bz2");
				
				if (fs::exists(file))
					fs::remove(file);
			}
			
			THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
				"DELETE FROM blast_cache WHERE db = ? ", -1,
				&deleteStmt, nullptr), mCacheDB);

			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(deleteStmt, 1,
				inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mCacheDB);
				
			THROW_IF_SQLITE3_ERROR(sqlite3_step(deleteStmt), mCacheDB);

			THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB,
				"INSERT OR REPLACE INTO blast_db_file(db, file, time_t) VALUES(?, ?, ?)", -1,
				&updateStmt, nullptr), mCacheDB);

			foreach (const fs::path& file, inFiles)
			{
				string path = file.string();
				
				THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(updateStmt, 1, inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mCacheDB);
				THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(updateStmt, 2, path.c_str(), path.length(), SQLITE_STATIC), mCacheDB);
				THROW_IF_SQLITE3_ERROR(sqlite3_bind_int64(updateStmt, 3, fs::last_write_time(file)), mCacheDB);
				
				THROW_IF_SQLITE3_ERROR(sqlite3_step(updateStmt), mCacheDB);
			}
		}
		catch (...)
		{
		}

		if (selectStmt != nullptr) sqlite3_finalize(selectStmt);
		if (deleteStmt != nullptr) sqlite3_finalize(deleteStmt);
		if (updateStmt != nullptr) sqlite3_finalize(updateStmt);
	}
}

string M6BlastCache::Submit(const string& inDatabank,
	string inQuery, string inMatrix, uint32 inWordSize,
	double inExpect, bool inLowComplexityFilter,
	bool inGapped, int32 inGapOpen, int32 inGapExtend,
	uint32 inReportLimit)
{
	boost::mutex::scoped_lock lock(mDbMutex);

	vector<fs::path> files;
	FastaFilesForDatabank(inDatabank, files);
	CheckCacheForDB(inDatabank, files);
	
	string result;
	
	sqlite3_reset(mSelectByParamsStmt);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mSelectByParamsStmt, 1, inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mSelectByParamsStmt, 2, inQuery.c_str(), inQuery.length(), SQLITE_STATIC), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mSelectByParamsStmt, 3, inMatrix.c_str(), inMatrix.length(), SQLITE_STATIC), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mSelectByParamsStmt, 4, inWordSize), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_double(mSelectByParamsStmt, 5, inExpect), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mSelectByParamsStmt, 6, inLowComplexityFilter), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mSelectByParamsStmt, 7, inGapped), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mSelectByParamsStmt, 8, inGapOpen), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mSelectByParamsStmt, 9, inGapExtend), mCacheDB);

	int err = sqlite3_step(mSelectByParamsStmt);
	if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
		THROW_IF_SQLITE3_ERROR(err, mCacheDB);

	if (err == SQLITE_ROW)
	{
		const char* text = reinterpret_cast<const char*>(sqlite3_column_text(mSelectByParamsStmt, 0));
		uint32 length = sqlite3_column_bytes(mSelectByParamsStmt, 0);

		result.assign(text, length);
		
		// now resubmit job if cache file is missing
		if (not fs::exists(mCacheDir / (result + ".xml.bz2")))
		{
			sqlite3_reset(mUpdateStatusStmt);
			
			string status = "queued";
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mUpdateStatusStmt, 1, status.c_str(), status.length(), SQLITE_STATIC), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mUpdateStatusStmt, 2, "", 0, SQLITE_STATIC), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mUpdateStatusStmt, 3, 0), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_double(mUpdateStatusStmt, 4, 0), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mUpdateStatusStmt, 5, result.c_str(), result.length(), SQLITE_STATIC), mCacheDB);
			
			err = sqlite3_step(mUpdateStatusStmt);
			if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
				THROW_IF_SQLITE3_ERROR(err, mCacheDB);

			mEmptyCondition.notify_one();
		}
	}
	else
	{
		boost::uuids::random_generator gen;
		boost::uuids::uuid id = gen();
		result = boost::lexical_cast<string>(id);
		
		sqlite3_reset(mInsertStmt);
		
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mInsertStmt, 1, result.c_str(), result.length(), SQLITE_STATIC), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mInsertStmt, 2, inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mInsertStmt, 3, inQuery.c_str(), inQuery.length(), SQLITE_STATIC), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mInsertStmt, 4, inMatrix.c_str(), inMatrix.length(), SQLITE_STATIC), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mInsertStmt, 5, inWordSize), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_double(mInsertStmt, 6, inExpect), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mInsertStmt, 7, inLowComplexityFilter), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mInsertStmt, 8, inGapped), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mInsertStmt, 9, inGapOpen), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mInsertStmt, 10, inGapExtend), mCacheDB);
		
		int err = sqlite3_step(mInsertStmt);
		if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
			THROW_IF_SQLITE3_ERROR(err, mCacheDB);
		
		mEmptyCondition.notify_one();
	}

	return result;
}

void M6BlastCache::Work()
{
	using namespace boost::posix_time;

	// wait a little at start up
	boost::this_thread::sleep(seconds(1));

	boost::mutex::scoped_lock lock(mDbMutex);
	
	for (;;)
	{
		try
		{
			int err = sqlite3_reset(mSelectByStatusStmt);
			if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
				THROW_IF_SQLITE3_ERROR(err, mCacheDB);
			
			err = sqlite3_step(mSelectByStatusStmt);
			if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
				THROW_IF_SQLITE3_ERROR(err, mCacheDB);

			if (err != SQLITE_ROW)
			{		
				mEmptyCondition.wait(lock);
				continue;
			}
			
			const char* text = reinterpret_cast<const char*>(sqlite3_column_text(mSelectByStatusStmt, 0));
			uint32 length = sqlite3_column_bytes(mSelectByStatusStmt, 0);
			string id(text, length);
			
			boost::thread thr(boost::bind(&M6BlastCache::ExecuteJob, this, id));

			mWorkCondition.wait(lock);

			thr.join();
		}
		catch (boost::thread_interrupted&)
		{
			break;
		}
		catch (exception& e)
		{
			cerr << e.what() << endl;
			boost::this_thread::sleep(seconds(5));
		}
		catch (...)
		{
			cerr << "unknown exception" << endl;
			boost::this_thread::sleep(seconds(5));
		}
	}
}

void M6BlastCache::ExecuteJob(const string& inJobID)
{
	try
	{
		SetJobStatus(inJobID, "running", "", 0, 0);
		
		sqlite3_reset(mFetchParamsStmt);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mFetchParamsStmt, 1, inJobID.c_str(), inJobID.length(), SQLITE_STATIC), mCacheDB);
		int err = sqlite3_step(mFetchParamsStmt);

		if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
			THROW_IF_SQLITE3_ERROR(err, mCacheDB);
		
		if (err != SQLITE_ROW)
			SetJobStatus(inJobID, "error", "job missing", 0, 0);
		else
		{
			// db, query, matrix, wordsize, expect, filter, gapped, gapopen, gapextend
			
			const char* text = reinterpret_cast<const char*>(sqlite3_column_text(mFetchParamsStmt, 0));
			uint32 length = sqlite3_column_bytes(mFetchParamsStmt, 0);
			string db(text, length);
			
			text = reinterpret_cast<const char*>(sqlite3_column_text(mFetchParamsStmt, 1));
			length = sqlite3_column_bytes(mFetchParamsStmt, 1);
			string query(text, length);

			text = reinterpret_cast<const char*>(sqlite3_column_text(mFetchParamsStmt, 2));
			length = sqlite3_column_bytes(mFetchParamsStmt, 2);
			string matrix(text, length);
			
			int wordsize = sqlite3_column_int(mFetchParamsStmt, 3);
			double expect = sqlite3_column_double(mFetchParamsStmt, 4);
			bool filter = sqlite3_column_int(mFetchParamsStmt, 5) != 0;
			bool gapped = sqlite3_column_int(mFetchParamsStmt, 6) != 0;
			int gapopen = sqlite3_column_int(mFetchParamsStmt, 7);
			int gapextend = sqlite3_column_int(mFetchParamsStmt, 8);
			
			vector<fs::path> files;
			FastaFilesForDatabank(db, files);
			
			M6BlastResultPtr result(M6Blast::Search(files, query, "blastp",
				matrix, wordsize, expect, filter, gapped, gapopen, gapextend, 250));
			
			zeep::xml::document doc("<blast-result/>");
			zeep::xml::serializer sr(doc.child(), false);
			sr & boost::serialization::make_nvp("blast-result", const_cast<M6Blast::Result&>(*result));
				
			fs::ofstream file(mCacheDir / (inJobID + ".xml.bz2"), ios_base::out|ios_base::trunc|ios_base::binary);
			io::filtering_stream<io::output> out;

			if (not file.is_open())
				throw runtime_error("could not create output file");
			
			out.push(io::bzip2_compressor());
			out.push(file);

			out << doc;
			
			CacheResult(inJobID, result);
			
			SetJobStatus(inJobID, "finished", "", result->mHits.size(),
				result->mHits.empty() or result->mHits.front().mHsps.empty() ? 0 : result->mHits.front().mHsps.front().mExpect);
		}
	}
	catch (exception& e)
	{
		SetJobStatus(inJobID, "error", e.what(), 0, 0);
	}
	
	mWorkCondition.notify_one();
}

void M6BlastCache::CacheResult(const string& inJobID, M6BlastResultPtr inResult)
{
	boost::mutex::scoped_lock lock(mCacheMutex);

	Cached c = { inJobID, inResult };
	mResultCache.push_front(c);
}

void M6BlastCache::SetJobStatus(const string inJobId, const string& inStatus, const string& inError,
	uint32 inHitCount, double inBestScore)
{
	boost::mutex::scoped_lock lock(mDbMutex);
	
	sqlite3_reset(mUpdateStatusStmt);
	
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mUpdateStatusStmt, 1, inStatus.c_str(), inStatus.length(), SQLITE_STATIC), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mUpdateStatusStmt, 2, inError.c_str(), inError.length(), SQLITE_STATIC), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(mUpdateStatusStmt, 3, inHitCount), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_double(mUpdateStatusStmt, 4, inBestScore), mCacheDB);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(mUpdateStatusStmt, 5, inJobId.c_str(), inJobId.length(), SQLITE_STATIC), mCacheDB);
	
	int err = sqlite3_step(mUpdateStatusStmt);
	if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
		THROW_IF_SQLITE3_ERROR(err, mCacheDB);
}

void M6BlastCache::Purge(bool inDeleteFiles)
{
	boost::mutex::scoped_lock lock(mDbMutex);
	
	ExecuteStatement("DELETE FROM blast_cache");
	
	if (inDeleteFiles)
	{
		// TODO implement
	}
}
