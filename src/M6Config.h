#pragma once

#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>

#include <zeep/xml/document.hpp>

namespace M6Config
{

class File
{
  public:
 							File();
 							~File();								

	static File&			Instance();

	void					WriteOut();
	
	zeep::xml::element*		GetDirectory(const std::string& inID);
	zeep::xml::element*		GetTool(const std::string& inID);
	zeep::xml::element*		GetUser(const std::string& inName, const std::string& inRealm);
	zeep::xml::element_set	GetServers();
	zeep::xml::element_set	GetFormats();
	zeep::xml::element*		GetFormat(const std::string& inID);
	zeep::xml::element_set	GetParsers();
	zeep::xml::element*		GetParser(const std::string& inID);
	zeep::xml::element_set	GetDatabanks();
	zeep::xml::element_set	GetDatabanks(const std::string& inID);
	zeep::xml::element*		GetDatabank(const std::string& inID);

  private:
	zeep::xml::element_set	Find(const boost::format& inFmt);
	zeep::xml::element*		FindFirst(const boost::format& inFmt);

	std::istream*			LoadDTD(const std::string& inBase,
								const std::string& inPublicID, const std::string& inSystemID);

	zeep::xml::document		mConfig;
};

void SetConfigFilePath(const boost::filesystem::path& inConfigFile);

// Prototypes

std::string						GetDirectory(const std::string& inID);
std::string						GetTool(const std::string& inID);
const zeep::xml::element*		GetUser(const std::string& inName, const std::string& inRealm);
const zeep::xml::element_set	GetServers();
const zeep::xml::element_set	GetFormats();
const zeep::xml::element*		GetFormat(const std::string& inID);
const zeep::xml::element_set	GetParsers();
const zeep::xml::element*		GetParser(const std::string& inID);
const zeep::xml::element_set	GetDatabanks();
const zeep::xml::element*		GetDatabank(const std::string& inID);
const zeep::xml::element_set	GetDatabanks(const std::string& inID);
std::string						GetDatabankParam(const std::string& inID, const std::string& inParam);
boost::filesystem::path			GetDbDirectory(const std::string& inDatabankID);

// Inlines

inline std::string GetDirectory(const std::string& inID)
{
	std::string result;
	zeep::xml::element* e = File::Instance().GetDirectory(inID);
	if (e != nullptr)
		result = e->content();
	return result;
}

inline std::string GetTool(const std::string& inID)
{
	std::string result;
	zeep::xml::element* e = File::Instance().GetTool(inID);
	if (e != nullptr)
		result = e->content();
	return result;
}

inline const zeep::xml::element* GetUser(const std::string& inName, const std::string& inRealm)
{
	return File::Instance().GetUser(inName, inRealm);
}

inline const zeep::xml::element_set GetServers()
{
	return File::Instance().GetServers();
}

inline const zeep::xml::element_set GetFormats()
{
	return File::Instance().GetFormats();
}

inline const zeep::xml::element* GetFormat(const std::string& inID)
{
	return File::Instance().GetFormat(inID);
}

inline const zeep::xml::element_set GetParsers()
{
	return File::Instance().GetParsers();
}

inline const zeep::xml::element* GetParser(const std::string& inID)
{
	return File::Instance().GetParser(inID);
}

inline const zeep::xml::element_set GetDatabanks()
{
	return File::Instance().GetDatabanks();
}

inline const zeep::xml::element_set	GetDatabanks(const std::string& inID)
{
	return File::Instance().GetDatabanks(inID);
}

inline const zeep::xml::element* GetDatabank(const std::string& inID)
{
	return File::Instance().GetDatabank(inID);
}

inline std::string GetDatabankParam(const std::string& inID, const std::string& inParam)
{
	std::string result;
	zeep::xml::element* db = File::Instance().GetDatabank(inID);
	if (db != nullptr)
	{
		zeep::xml::node* n = db->find_first_node(inParam.c_str());
		if (n != nullptr)
			result = n->str();
	}
	return result;
}

}
