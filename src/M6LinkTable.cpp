#include "M6Lib.h"

#include <iostream>

#include <sqlite3.h>

#include <boost/filesystem/operations.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Config.h"
#include "M6Error.h"
#include "M6LinkTable.h"

using namespace std;
namespace fs = boost::filesystem;

//	Problem: we have text documents containing references to other text documents.
//	The question may arise, what documents do have references to each other, or more
//	specifically, what documents is my document linked to?
//	To find the answer we need to know what documents are mentioned in my document,
//	and what documents mention my document.
//	So we need a table to find the links out for a certain document, and we need
//	a table containing links in for all referenced documents. For the sake of simplicity
//	we use one table for both.
//
//	So, if we want to find all documents linked to CRAM_CRAAB in SwissProt, we have
//	to search for links out:
//
//	SELECT linked_db, linked_id FROM links WHERE my_id = 'CRAM_CRAAB';
//
//	and join the results with the links in:
//
//	foreach (databank, loaded_databanks)
//		SELECT 'databank', my_id FROM links WHERE linked_db = 'sprot' AND linked_id = 'CRAM_CRAAB';
//

namespace
{
// --------------------------------------------------------------------

void ThrowDbException(sqlite3* conn, int err, const char* stmt, const char* file, int line, const char* func)
{
	const char* errmsg = sqlite3_errmsg(conn);
	if (VERBOSE)
		cerr << "sqlite3 error '" << errmsg << "' in " << file << ':' << line << ": " << func << endl;
	throw M6Exception(errmsg);
}

#define THROW_IF_SQLITE3_ERROR(e,conn) \
	do { int _e(e); if (_e != SQLITE_OK and _e != SQLITE_DONE) ThrowDbException(conn, e, #e, __FILE__, __LINE__, BOOST_CURRENT_FUNCTION); } while (false)

}

M6LinkTable::M6LinkTable(const string& inDatabank, const boost::filesystem::path& inLinkDB)
	: mDatabank(inDatabank), mDb(nullptr)
{
	// only open read/write when the databank does not exist yet
	if (fs::exists(inLinkDB))
	{
		THROW_IF_SQLITE3_ERROR(sqlite3_open_v2(inLinkDB.string().c_str(), &mDb,
			SQLITE_OPEN_READONLY, nullptr), nullptr);

		sqlite3_stmt* stmt;
		THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mDb, inSQL, -1, &stmt, nullptr), mDb);
		
		while (sqlite3_step(stmt) == SQLITE_ROW)
		{
			const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
			uint32 length = sqlite3_column_bytes(stmt, 0);
			string db(text, length);
			
			mLinkedDbs.insert(db);
		}
		
		sqlite3_finalize(stmt.second);
	}
	else
	{
		THROW_IF_SQLITE3_ERROR(sqlite3_open_v2(inLinkDB.string().c_str(), &mDb,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr), nullptr);

		ExecuteStatement(
			"CREATE TABLE links ("
			"	linked_db TEXT,"
			"	linked_id TEXT,"
			"	my_id TEXT"
			")"
		);
	}

	sqlite3_extended_result_codes(mDb, true);
}

M6LinkTable::~M6LinkTable()
{
	if (mDb != nullptr)
		Close();
}

void M6LinkTable::AddLink(const string& inMyID, const string& inLinkedDB, const string& inLinkedID)
{
	boost::mutex::scoped_lock lock(mLock);
	sqlite3_stmt* stmt = Prepare("INSERT INTO links (my_id, linked_db, linked_id) VALUES (?, ?, ?)");
	assert(stmt);
	
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 1, inMyID.c_str(), inMyID.length(), SQLITE_STATIC), mDb);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 2, inLinkedDB.c_str(), inLinkedDB.length(), SQLITE_STATIC), mDb);
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 3, inLinkedID.c_str(), inLinkedID.length(), SQLITE_STATIC), mDb);

	int err = sqlite3_step(stmt);
	if (err != SQLITE_OK and err != SQLITE_DONE)
		THROW_IF_SQLITE3_ERROR(err, mDb);
}

void M6LinkTable::Finish()
{
	ExecuteStatement("CREATE INDEX my_ix ON links(my_id)");
	ExecuteStatement("CREATE INDEX linked_ix ON links(linked_db,linked_id)");
	
	Close();
}

void M6LinkTable::Close()
{
	foreach (auto stmt, mStatements)
		sqlite3_finalize(stmt.second);
	
	sqlite3_close(mDb);
	mDb = nullptr;
}

sqlite3_stmt* M6LinkTable::Prepare(const char* inSQL)
{
	auto i = mStatements.find(inSQL);
	if (i == mStatements.end())
	{
		sqlite3_stmt* stmt;
		
		THROW_IF_SQLITE3_ERROR(sqlite3_prepare_v2(mDb, inSQL, -1, &stmt, nullptr), mDb);
		
		i = mStatements.insert(make_pair(inSQL, stmt)).first;
	}
	
	THROW_IF_SQLITE3_ERROR(sqlite3_reset(i->second), mDb);
	
	return i->second;
}

void M6LinkTable::ExecuteStatement(const char* inStatement)
{
	if (VERBOSE)
		cout << inStatement << endl;

	char* errmsg = NULL;
	int err = sqlite3_exec(mDb, inStatement, nullptr, nullptr, &errmsg);
	if (errmsg != nullptr)
	{
		cerr << errmsg << endl;
		sqlite3_free(errmsg);
	}
	
	THROW_IF_SQLITE3_ERROR(err, mDb);
}

void M6LinkTable::GetLinkedDbs(const string& inID, vector<string>& outDatabanks)
{
	boost::mutex::scoped_lock lock(mLock);

	sqlite3_stmt* stmt = Prepare("SELECT DISTINCT linked_db FROM links WHERE id = ?");
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 1, inID.c_str(), inID.length(), SQLITE_STATIC), mDb);
	
	outDatabanks.clear();
	
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
		uint32 length = sqlite3_column_bytes(stmt, 0);
		string db(text, length);
		
		outDatabanks.push_back(db);
	}
}

void M6LinkTable::GetLinksIn(const string& inDatabank, const string& inID, vector<string>& outIDs)
{
	if (mLinkedDbs.count(inDatabank))
	{
		boost::mutex::scoped_lock lock(mLock);
		
		sqlite3_stmt* stmt = Prepare("SELECT my_id FROM links WHERE linked_db = ? AND linked_id = ?");
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 1, inDatabank.c_str(), inDatabank.length(), SQLITE_STATIC), mDb);
		THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 2, inID.c_str(), inID.length(), SQLITE_STATIC), mDb);
		
		outIDs.clear();
		
		while (sqlite3_step(stmt) == SQLITE_ROW)
		{
			const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
			uint32 length = sqlite3_column_bytes(stmt, 0);
			string id(text, length);
			
			outIDs.push_back(id);
		}
	}
}

void M6LinkTable::GetLinksOut(string& inID, vector<pair<string,string>>& outLinks)
{
	boost::mutex::scoped_lock lock(mLock);
	
	sqlite3_stmt* stmt = Prepare("SELECT linked_db, linked_id FROM links WHERE my_id = ?");
	THROW_IF_SQLITE3_ERROR(sqlite3_bind_text(stmt, 1, inID.c_str(), inID.length(), SQLITE_STATIC), mDb);
	
	outLinks.clear();
	
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
		uint32 length = sqlite3_column_bytes(stmt, 0);
		string db(text, length);
		
		text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
		length = sqlite3_column_bytes(stmt, 1);
		string id(text, length);
		
		outLinks.push_back(make_pair(db, id));
	}
}

