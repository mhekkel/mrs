#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>

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

zx::element* M6Config::LoadConfig(const std::string& inDatabank)
{
	string dbConfigPath = (boost::format("/m6-config/databank[@id='%1%']") % inDatabank).str();
	auto dbConfig = mConfig->find(dbConfigPath);
	if (dbConfig.empty())
		THROW(("databank %s not specified in config file", inDatabank.c_str()));
	
	if (dbConfig.size() > 1)
		THROW(("databank %s specified multiple times in config file", inDatabank.c_str()));
	
	return dbConfig.front();
}

zx::element* M6Config::LoadParser(const std::string& inParser)
{
	string dbConfigPath = (boost::format("/m6-config/parser[@id='%1%']") % inParser).str();
	auto dbConfig = mConfig->find(dbConfigPath);
	if (dbConfig.empty())
		THROW(("parser %s not specified in config file", inParser.c_str()));
	
	if (dbConfig.size() > 1)
		THROW(("parser %s specified multiple times in config file", inParser.c_str()));
	
	return dbConfig.front();
}

zeep::xml::element_set M6Config::LoadServers()
{
	return mConfig->find("/m6-config/server");
}
