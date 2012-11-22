#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/bind.hpp>

#include <zeep/xml/writer.hpp>

#include "M6Config.h"
#include "M6Error.h"

using namespace std;
namespace zx = zeep::xml;
namespace fs = boost::filesystem;

// --------------------------------------------------------------------

namespace M6Config
{

fs::path sConfigFile = "config/m6-config.xml";
File* sInstance;

File::File()
{
	if (not fs::exists(sConfigFile))
		THROW(("Configuration file not found (\"%s\")", sConfigFile.string().c_str()));

	istream*			LoadDTD(
							const string&		inBase,
							const string&		inPublicID,
							const string&		inSystemID);

	
	fs::ifstream configFileStream(sConfigFile, ios::binary);
	mConfig.external_entity_ref_handler = boost::bind(&File::LoadDTD, this, _1, _2, _3);
	mConfig.set_validating(true);
	configFileStream >> mConfig;
}

File::~File()
{
}

void SetConfigFilePath(const fs::path& inConfigFile)
{
	sConfigFile = inConfigFile;
}

istream* File::LoadDTD(const string& inBase, const string& inPublicID, const string& inSystemID)
{
	if (inSystemID == "http://mrs.cmbi.ru.nl/dtd/m6-config.dtd")
	{
		fs::path p(sConfigFile.parent_path() / "m6-config.dtd");
		return new fs::ifstream(p);
	}
	
	return nullptr;
}

File& File::Instance()
{
	if (sInstance == nullptr)
		sInstance = new File;
	return *sInstance;
}

void File::WriteOut()
{
	fs::path dir(sConfigFile.parent_path());
	boost::format name(sConfigFile.stem().string() + "-%1%" + sConfigFile.filename().extension().string());

	uint32 i = 1;
	while (fs::exists((name % i).str()))
		++i;
	
	boost::system::error_code ec;
	
	while (i-- > 1)
		fs::rename((name % i).str(), (name % (i + 1)).str());
	
	fs::rename(sConfigFile, dir / (name % 1).str(), ec);
	if (ec)
		THROW(("Failed to create backup file for configuration file"));

	fs::ofstream configFileStream(sConfigFile, ios::binary);
	
	zx::writer w(configFileStream);
	w.set_xml_decl(true);
	w.set_indent(false);
	w.set_trim(false);
	w.set_escape_whitespace(false);
	w.set_wrap(false);

	mConfig.write(w);
}	

zx::element_set File::Find(const boost::format& inFmt)
{
	return mConfig.find(inFmt.str());
}

zx::element* File::FindFirst(const boost::format& inFmt)
{
	return mConfig.find_first(inFmt.str());
}

zx::element* File::GetDirectory(const string& inID)
{
	return FindFirst(boost::format("/m6-config/directories/directory[@id='%1%']") % inID);
}

zx::element* File::GetTool(const string& inID)
{
	return FindFirst(boost::format("/m6-config/tools/tool[@id='%1%']") % inID);
}

zx::element* File::GetUser(const string& inName, const string& inRealm)
{
	return FindFirst(boost::format("/m6-config/users/user[@name='%1%' and @realm='%2%']") % inName % inRealm);
}

zx::element* File::GetServer()
{
	return FindFirst(boost::format("/m6-config/server"));
}

zx::element_set File::GetFormats()
{
	return Find(boost::format("/m6-config/formats/format"));
}

zx::element* File::GetFormat(const string& inID)
{
	return FindFirst(boost::format("/m6-config/formats/format[@id='%1%']") % inID);
}

zx::element_set File::GetParsers()
{
	return Find(boost::format("/m6-config/parsers/parser"));
}

zx::element* File::GetParser(const string& inID)
{
	return FindFirst(boost::format("/m6-config/parsers/parser[@id='%1%']") % inID);
}

zx::element_set File::GetDatabanks()
{
	return Find(boost::format("/m6-config/databanks/databank"));
}

zx::element_set File::GetDatabanks(const string& inID)
{
	zx::element_set result = Find(boost::format("/m6-config/databanks/databank[@id='%1%' and @enabled='true']") % inID);
	if (result.empty())
		result = Find(boost::format("/m6-config/databanks/databank[aliases/alias='%1%' and @enabled='true']") % inID);
	return result;
}

zx::element* File::GetDatabank(const string& inID)
{
	return FindFirst(boost::format("/m6-config/databanks/databank[@id='%1%' and @enabled='true']") % inID);
}

void Reload()
{
	unique_ptr<File> newFile(new File);
	delete sInstance;
	sInstance = newFile.release();
}

fs::path GetDbDirectory(const std::string& inDatabankID)
{
	fs::path mrsdir(GetDirectory("mrs"));
	return mrsdir / (inDatabankID + ".m6");
}

}
