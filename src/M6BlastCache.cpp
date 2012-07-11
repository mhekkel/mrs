#include "M6Lib.h"

#include <iostream>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/current_function.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>

#include <sqlite3.h>

#include "M6Config.h"
#include "M6Error.h"
#include "M6BlastCache.h"

using namespace std;
namespace fs = boost::filesystem;

// --------------------------------------------------------------------

void ThrowDbException(sqlite3* conn, int err, const char* stmt, const char* file, int line, const char* func)
{
	const char* errmsg = sqlite3_errmsg(conn);
	cerr << "sqlite3 error '" << errmsg << "' in " << file << ':' << line << ": " << func << endl;
	throw M6Exception(errmsg);
}

#define THROW_IF_SQLITE3_ERROR(e,conn) \
	do { int _e(e); if (_e != SQLITE_OK) ThrowDbException(conn, e, #e, __FILE__, __LINE__, BOOST_CURRENT_FUNCTION); } while (false)

// --------------------------------------------------------------------

M6BlastCache& M6BlastCache::Instance()
{
	static M6BlastCache sInstance;
	return sInstance;
}

M6BlastCache::M6BlastCache()
	: mCacheDB(nullptr)
	, mWorkerThread([this](){ this->Work(); })
{
	string file = M6Config::Instance().FindGlobal("//blast-cache");
	if (file.empty())
		file = M6Config::Instance().FindGlobal("//srvdir") + "blast-cache";
		
	mCacheDir = fs::path(file);
	if (not fs::exists(mCacheDir))
		fs::create_directory(mCacheDir);
	file = (mCacheDir / "index.db").string();

	sqlite3_config(SQLITE_CONFIG_MULTITHREAD);

	THROW_IF_SQLITE3_ERROR(sqlite3_open_v2(file.c_str(), &mCacheDB,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr), nullptr);

	sqlite3_extended_result_codes(mCacheDB, true);
	
	ExecuteStatement(
		"CREATE TABLE IF NOT EXISTS blast_cache ( "
			"id TEXT, "
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
}

M6BlastCache::~M6BlastCache()
{
	if (mWorkerThread.joinable())
	{
		mWorkerThread.interrupt();
		mWorkerThread.join();
	}

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

M6BlastJobStatus M6BlastCache::JobStatus(const boost::uuids::uuid& inJobID)
{
	return bj_Unknown;
}

string M6BlastCache::JobError(const boost::uuids::uuid& inJobID)
{
	return "unknown";
}

const M6Blast::Result* M6BlastCache::JobResult(const boost::uuids::uuid& inJobID)
{
	return nullptr;
}

boost::uuids::uuid M6BlastCache::Submit(const string& inDatabank,
	string inQuery, string inMatrix, uint32 inWordSize,
	double inExpect, bool inLowComplexityFilter,
	bool inGapped, uint32 inGapOpen, uint32 inGapExtend,
	uint32 inReportLimit)
{
	boost::uuids::uuid result;
	sqlite3_stmt* stmt = nullptr;
	
	try
	{
		const char kSelect[] = "SELECT id FROM blast_cache WHERE "
			"db = ? AND query = ? AND matrix = ? AND wordsize = ? AND expect = ? AND filter = ? AND gapped = ? AND gapopen = ? AND gapextend = ? ";
	
		THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB, kSelect, -1, &stmt, nullptr), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 1, inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 2, inQuery.c_str(), inQuery.length(), SQLITE_STATIC), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 3, inMatrix.c_str(), inMatrix.length(), SQLITE_STATIC), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 4, inWordSize), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_double(stmt, 5, inExpect), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 6, inLowComplexityFilter), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 7, inGapped), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 8, inGapOpen), mCacheDB);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 9, inGapExtend), mCacheDB);
	
		int err = sqlite3_step(stmt);
		if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
			THROW_IF_SQLITE3_ERROR(err, mCacheDB);

		if (err == SQLITE_ROW)
		{
			const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
			uint32 length = sqlite3_column_bytes(stmt, 0);

			result = boost::lexical_cast<boost::uuids::uuid>(string(text, length));
		}
		else
		{
			boost::uuids::random_generator gen;
			result = gen();
			string id = boost::lexical_cast<string>(result);
			
			const char kInsert[] =
				"INSERT INTO blast_cache (id, db, query, matrix, wordsize, expect, filter, gapped, gapopen, gapextend) "
				"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
		
			sqlite3_finalize(stmt);
			THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mCacheDB, kInsert, -1, &stmt, nullptr), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 1, id.c_str(), id.length(), SQLITE_STATIC), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 2, inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 3, inQuery.c_str(), inQuery.length(), SQLITE_STATIC), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 4, inMatrix.c_str(), inMatrix.length(), SQLITE_STATIC), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 5, inWordSize), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_double(stmt, 6, inExpect), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 7, inLowComplexityFilter), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 8, inGapped), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 9, inGapOpen), mCacheDB);
			THROW_IF_SQLITE3_ERROR(sqlite3_bind_int(stmt, 10, inGapExtend), mCacheDB);
			
			int err = sqlite3_step(stmt);
			if (err != SQLITE_OK and err != SQLITE_ROW and err != SQLITE_DONE)
				THROW_IF_SQLITE3_ERROR(err, mCacheDB);
		}
	}
	catch (...)
	{
		if (stmt != nullptr)
			sqlite3_finalize(stmt);
		
		throw;
	}

	sqlite3_finalize(stmt);
	
	return result;
}

void M6BlastCache::Work()
{
	using namespace boost::posix_time;

	// wait a little at start up
	boost::this_thread::sleep(seconds(5));
	
	for (;;)
	{
		boost::this_thread::sleep(seconds(5));
		
	}
}

void M6BlastCache::ExecuteJob(const boost::uuids::uuid& inJobID)
{
	
}
