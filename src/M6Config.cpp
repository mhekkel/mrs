#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

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

File::File()
	: mConfig(nullptr)
{
	if (not fs::exists(sConfigFile))
		THROW(("Configuration file not found (\"%s\")", sConfigFile.string().c_str()));
	
	fs::ifstream configFileStream(sConfigFile, ios::binary);
	mConfig = new zx::document(configFileStream);
}

File::~File()
{
	delete mConfig;
}

void SetConfigFilePath(const fs::path& inConfigFile)
{
	sConfigFile = inConfigFile;
}

File& File::Instance()
{
	static File sInstance;
	return sInstance;
}

void File::WriteOut()
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

zx::element_set File::Find(const boost::format& inFmt)
{
	zx::element_set result;
	if (mConfig != nullptr)
		result = mConfig->find(inFmt.str());
	return result;
}

zx::element* File::FindFirst(const boost::format& inFmt)
{
	zx::element* result = nullptr;
	if (mConfig != nullptr)
		result = mConfig->find_first(inFmt.str());
	return result;
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

zx::element_set File::GetServers()
{
	return Find(boost::format("/m6-config/servers/server"));
}

zx::element* File::GetFormat(const string& inID)
{
	return FindFirst(boost::format("/m6-config/formats/format[@id='%1%']") % inID);
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
	zx::element_set result = Find(boost::format("/m6-config/databanks/databank[@id='%1%']") % inID);
	if (result.empty())
		result = Find(boost::format("/m6-config/databanks/databank[aliases/alias='%1%']") % inID);
	return result;
}

zx::element* File::GetDatabank(const string& inID)
{
	return FindFirst(boost::format("/m6-config/databanks/databank[@id='%1%']") % inID);
}

}
