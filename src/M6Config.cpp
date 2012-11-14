#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>

#include <zeep/xml/writer.hpp>

#include "M6Config.h"
#include "M6Error.h"

using namespace std;
namespace zx = zeep::xml;
namespace fs = boost::filesystem;

// --------------------------------------------------------------------

fs::path M6Config::sConfigFile = "config/m6-config.xml";

void M6Config::SetConfigFile(const fs::path& inConfigFile)
{
	sConfigFile = inConfigFile;
}

M6Config& M6Config::Instance()
{
	static M6Config sInstance;
	return sInstance;
}

M6Config::M6Config()
	: mConfig(nullptr)
{
	if (not fs::exists(sConfigFile))
		THROW(("Configuration file not found (\"%s\")", sConfigFile.string().c_str()));
	
	fs::ifstream configFileStream(sConfigFile, ios::binary);
	mConfig = new zx::document(configFileStream);
}

M6Config::~M6Config()
{
	delete mConfig;
}

zx::element* M6Config::LoadDatabank(const string& inDatabank)
{
	string dbConfigPath = (boost::format("/m6-config/databank[@id='%1%']") % inDatabank).str();
	auto dbConfig = mConfig->find(dbConfigPath);
	if (dbConfig.empty())
		THROW(("databank %s not specified in config file", inDatabank.c_str()));
	
	if (dbConfig.size() > 1)
		THROW(("databank %s specified multiple times in config file", inDatabank.c_str()));
	
	return dbConfig.front();
}

zx::element* M6Config::LoadParser(const string& inParser)
{
	string dbConfigPath = (boost::format("/m6-config/parser[@id='%1%']") % inParser).str();
	auto dbConfig = mConfig->find(dbConfigPath);
	if (dbConfig.empty())
		THROW(("parser %s not specified in config file", inParser.c_str()));
	
	if (dbConfig.size() > 1)
		THROW(("parser %s specified multiple times in config file", inParser.c_str()));
	
	return dbConfig.front();
}

zeep::xml::element_set M6Config::LoadDatabanks()
{
	return mConfig->find("/m6-config/databank");
}

zeep::xml::element_set M6Config::LoadServers()
{
	return mConfig->find("/m6-config/server");
}

zeep::xml::element_set M6Config::Find(const string& inXPath)
{
	return mConfig->find(inXPath);
}

zeep::xml::element* M6Config::FindFirst(const string& inXPath)
{
	return mConfig->find_first(inXPath);
}

string M6Config::FindGlobal(const string& inXPath)
{
	zeep::xml::element* e = mConfig->find_first(inXPath);
	string result;
	if (e != nullptr)
		result = e->content();
	return result;
}

zeep::xml::element* M6Config::LoadFormat(const string& inDatabank)
{
	zeep::xml::element* result = nullptr;
	
	for (;;)
	{
		string dbConfigPath = (boost::format("/m6-config/databank[@id='%1%']") % inDatabank).str();
		auto dbConfig = mConfig->find(dbConfigPath);
		if (dbConfig.empty() or dbConfig.size() > 1)
			break;

		string format = dbConfig.front()->get_attribute("format");
		if (format.empty())
			break;
		
		string formatPath = (boost::format("/m6-config/format[@id='%1%']") % format).str();
		auto formatConfig = mConfig->find(formatPath);
		if (formatConfig.empty() or formatConfig.size() > 1)
			break;
	
		result = formatConfig.front();
		break;
	}
	
	return result;
}

string M6Config::LoadFormatScript(const string& inDatabank)
{
	string result;
	
	for (;;)
	{
		string dbConfigPath = (boost::format("/m6-config/databank[@id='%1%']") % inDatabank).str();
		auto dbConfig = mConfig->find(dbConfigPath);
		if (dbConfig.empty() or dbConfig.size() > 1)
			break;
		
		string format = dbConfig.front()->get_attribute("format");
		if (format.empty())
			break;
		
		string formatPath = (boost::format("/m6-config/format[@id='%1%']/script") % format).str();
		auto formatConfig = mConfig->find(formatPath);
		if (formatConfig.empty() or formatConfig.size() > 1)
			break;
	
		result = formatConfig.front()->content();
	
		break;	
	}
	
	return result;
}

void M6Config::SetPassword(const string& inRealm, const string& inUser, const string& inPassword)
{
	string xp = (boost::format("/m6-config/users/user[@name = '%1%' and @realm = '%2%']") % inUser % inRealm).str();
	zx::element* user = mConfig->find_first(xp);
	if (user == nullptr)
		THROW(("User not found in config file"));

	user->set_attribute("password", inPassword);

	WriteOut();
}

void M6Config::WriteOut()
{
	fs::path dir(sConfigFile.parent_path());
	boost::format name(sConfigFile.filename().stem().string() + "-%1%" + sConfigFile.filename().extension().string());

	uint32 i = 1;
	while (fs::exists(dir / (name % i).str()))
		++i;
	
	boost::system::error_code ec;
	fs::rename(sConfigFile, dir / (name % i).str(), ec);
	if (ec)
		THROW(("Failed to create backup file for configuration file"));

	fs::ofstream configFileStream(sConfigFile, ios::binary);
	
	zx::writer w(configFileStream);
	w.set_xml_decl(true);
	w.set_indent(false);
	w.set_trim(false);
	w.set_escape_whitespace(false);
	w.set_wrap(false);

	mConfig->write(w);
}	
