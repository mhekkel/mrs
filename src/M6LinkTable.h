#pragma once

#include <map>

#include <zeep/xml/node.hpp>

namespace zx = zeep::xml;

struct sqlite3;
struct sqlite3_stmt;
class M6Iterator;

class M6LinkTable
{
  public:

	static M6LinkTable&	Instance();
	
	std::vector<std::string>
					GetLinkedDbs(const std::string& inDatabank);
	
	std::vector<std::string>
					GetLinkedDbs(const std::string& inDatabank,
						const std::string& inID);

	M6Iterator*		GetLinks(const std::string& inDatabank,
						const std::string& inID, const std::string& inTargetDatabank);	

	void			StartUpdate(const std::string& inDatabank);
	void			CommitUpdate();
	void			RollbackUpdate();
	void			AddLink(const std::string& inDatabank1, const std::string& inID1,
						const std::string& inDatabank2, const std::string& inID2);
	
  private:
					M6LinkTable(zx::element* inConfig);
					~M6LinkTable();

	sqlite3_stmt*	Prepare(const char* inSQL);

	sqlite3*		mDb;
	std::map<const char*,sqlite3_stmt*>
					mStatements;
};
