#pragma once

#include <map>
#include <set>

#include <boost/thread/mutex.hpp>
#include <boost/filesystem/path.hpp>

#include <zeep/xml/node.hpp>

namespace zx = zeep::xml;

struct sqlite3;
struct sqlite3_stmt;
class M6Iterator;

class M6LinkTable
{
  public:
					M6LinkTable(const std::string& inDatabank,
						boost::filesystem::path& inLinkDB);
					~M6LinkTable();

	void			AddLink(const std::string& inMyID,
						const std::string& inLinkedDB, const std::string& inLinkedID);
	void			Finish();
	
	void			GetLinkedDbs(std::vector<std::string>& outDatabanks)
						{ outDatabanks.assign(mLinkedDbs.begin(), mLinkedDbs.end()); }

	void			GetLinkedDbs(const std::string& inID, std::vector<std::string>& outDatabanks);

	void			GetLinksIn(const std::string& inDatabank, const std::string& inID,
						std::vector<std::string>& outIDs);

	void			GetLinksOut(std::string& inID,
						std::vector<std::pair<std::string,std::string>>& outLinks);
	
  private:

					M6LinkTable(const M6LinkTable&);
	M6LinkTable&	operator=(const M6LinkTable&);

	sqlite3_stmt*	Prepare(const char* inSQL);
	void			ExecuteStatement(const char* inStatement);
	void			Close();

	boost::mutex	mLock;
	std::string		mDatabank;
	sqlite3*		mDb;
	std::map<const char*,sqlite3_stmt*>
					mStatements;
	std::set<std::string>
					mLinkedDbs;
};
