#include "M6Lib.h"

#include "M6Config.h"
#include "M6LinkTable.h"

using namespace std;

M6LinkTable& M6LinkTable::Instance()
{
	static M6LinkTable sInstance(M6Config::Instance().FindFirst("/m6-config/links"));
	return sInstance;
}

M6LinkTable::M6LinkTable(zx::element* inConfig)
{
// CREATE TABLE link IF NOT EXISTS (
//		sdb TEXT,
//		sid INTEGER,
//		tdb TEXT,
//		sid TEXT
//	);
}

M6LinkTable::~M6LinkTable()
{
}

vector<string> M6LinkTable::GetLinkedDbs(const string& inDatabank)
{
//	SELECT sdb AS db FROM link WHERE tdb = ?
//	UNION
//	SELECT tdb AS db FROM link WHERE sdb = ?

	return vector<string>();
}

vector<string> M6LinkTable::GetLinkedDbs(const string& inDatabank, const string& inID)
{
//	SELECT * FROM link WHERE sdb = ? AND sid = ?
	return vector<string>();
}

M6Iterator* M6LinkTable::GetLinks(const string& inDatabank,
	const string& inID, const string& inTargetDatabank)
{
	return nullptr;
}

void M6LinkTable::StartUpdate(const string& inDatabank)
{
}

void M6LinkTable::CommitUpdate()
{
}

void M6LinkTable::AddLink(const string& inDatabank1, const string& inID1,
	const string& inDatabank2, const string& inID2)
{
}
