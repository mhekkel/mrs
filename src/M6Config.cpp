//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <sstream>
#include <functional>

#include "boost/date_time/local_time/local_time.hpp"
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/regex.hpp>

#include <zeep/xml/writer.hpp>

#include "M6Config.h"
#include "M6Error.h"

using namespace std;
using namespace placeholders;
namespace zx = zeep::xml;
namespace fs = boost::filesystem;

#ifndef MRS_ETC_DIR
#error MRS_ETC_DIR is not defined
#endif

// --------------------------------------------------------------------

namespace M6Config
{

fs::path sConfigFile = MRS_ETC_DIR "/mrs-config.xml";
unique_ptr<File> sInstance;

File::File()
{
    if (not fs::exists(sConfigFile))
        THROW(("Configuration file not found (\"%s\")", sConfigFile.string().c_str()));

    istream*            LoadDTD(
                            const string&        inBase,
                            const string&        inPublicID,
                            const string&        inSystemID);


    fs::ifstream configFileStream(sConfigFile, ios::binary);
    mConfig.external_entity_ref_handler = bind(&File::LoadDTD, this, _1, _2, _3);
    mConfig.set_validating(true);
    configFileStream >> mConfig;
}

File::File(const File& inFile)
{
    zx::element* config = inFile.mConfig.child();
    mConfig.child(static_cast<zx::element*>(config->clone()));
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
    if (inSystemID == "https://mrs.cmbi.umcn.nl/dtd/mrs-config.dtd")
    {
        fs::path p(sConfigFile.parent_path() / "mrs-config.dtd");
        return new fs::ifstream(p);
    }

    return nullptr;
}

File& File::Instance()
{
    if (not sInstance)
        sInstance.reset(new File);
    return *sInstance;
}

void File::Validate()
{
    stringstream xml;

    zx::writer w(xml, true);
    w.xml_decl(false);
    w.doctype("mrs-config", "", "https://mrs.cmbi.umcn.nl/dtd/mrs-config.dtd");
    mConfig.write(w);

    zx::document doc;
    doc.external_entity_ref_handler = bind(&File::LoadDTD, this, _1, _2, _3);
    doc.set_validating(true);
    xml >> doc;
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

    zx::writer w(configFileStream, true);
    w.xml_decl(false);
    w.doctype("mrs-config", "", "https://mrs.cmbi.umcn.nl/dtd/mrs-config.dtd");
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
    zx::element* e = FindFirst(boost::format("/mrs-config/directories/directory[@id='%1%']") % inID);
    if (e == nullptr)
    {
        e = new zx::element("directory");
        e->set_attribute("id", inID);

        zx::element* p = mConfig.find_first("/mrs-config/directories");
        // p exists, otherwise the configfile would not validate
        p->append(e);
    }

    return e;
}

zx::element* File::GetTool(const string& inID)
{
    zx::element* e = FindFirst(boost::format("/mrs-config/tools/tool[@id='%1%']") % inID);
    if (e == nullptr)
    {
        e = new zx::element("tool");
        e->set_attribute("id", inID);

        zx::element* p = mConfig.find_first("/mrs-config/tools");
        // p exists, otherwise the configfile would not validate
        p->append(e);
    }

    return e;
}

zx::element* File::GetSchedule()
{
    return mConfig.find_first("/mrs-config/scheduler");
}
zx::element* File::GetLogger()
{
    return mConfig.find_first("/mrs-config/logger");
}

zx::element* File::GetUser(const string& inName, const string& inRealm)
{
    return FindFirst(boost::format("/mrs-config/users/user[@name='%1%' and @realm='%2%']") % inName % inRealm);
}

zx::element* File::CreateUser(const string& inName, const string& inRealm)
{
    zx::element* user = FindFirst(boost::format("/mrs-config/users/user[@name='%1%' and @realm='%2%']") % inName % inRealm);
    if (user == nullptr)
    {
        zx::element* users = mConfig.find_first("/mrs-config/users");

        user = new zx::element("user");
        user->set_attribute("name", inName);
        user->set_attribute("realm", inRealm);
        users->append(user);
    }
    return user;
}

zx::element* File::GetServer()
{
    return FindFirst(boost::format("/mrs-config/server"));
}

zx::element_set File::GetFormats()
{
    return Find(boost::format("/mrs-config/formats/format"));
}

zx::element* File::GetFormat(const string& inID)
{
    return FindFirst(boost::format("/mrs-config/formats/format[@id='%1%']") % inID);
}

zx::element* File::CreateFormat()
{
    zx::element* result = new zx::element("format");

    boost::format testId("/mrs-config/formats/format[id='format-%1%']");
    uint32 nr = 1;
    while (FindFirst(testId % nr) != nullptr)
        ++nr;

    result->set_attribute("id", (boost::format("format-%1%") % nr).str());

    zx::element* formats = mConfig.find_first("/mrs-config/formats");
    formats->append(result);

    return result;
}

zx::element_set File::GetParsers()
{
    return Find(boost::format("/mrs-config/parsers/parser"));
}

zx::element* File::GetParser(const string& inID)
{
    return FindFirst(boost::format("/mrs-config/parsers/parser[@id='%1%']") % inID);
}

zx::element_set File::GetDatabanks()
{
    return Find(boost::format("/mrs-config/databanks/databank"));
}

zx::element_set File::GetDatabanks(const string& inID)
{
    zx::element_set result = Find(boost::format("/mrs-config/databanks/databank[@id='%1%' and @enabled='true']") % inID);
    if (result.empty())
        result = Find(boost::format("/mrs-config/databanks/databank[aliases/alias='%1%' and @enabled='true']") % inID);
    return result;
}

zx::element* File::GetEnabledDatabank(const string& inID)
{
    zx::element* db = GetConfiguredDatabank(inID);
    if (db == nullptr)
        THROW(("Databank %s not configured", inID.c_str()));

    if (db->get_attribute("enabled") != "true")
        THROW(("Databank %s not enabled", inID.c_str()));

    return db;
}

zx::element* File::GetConfiguredDatabank(const string& inID)
{
    return FindFirst(boost::format("/mrs-config/databanks/databank[@id='%1%']") % inID);
}

zx::element* File::CreateDatabank()
{
    zx::element* result = new zx::element("databank");

    boost::format testId("/mrs-config/databanks/databank[id='databank-%1%']");
    uint32 nr = 1;
    while (FindFirst(testId % nr) != nullptr)
        ++nr;

    result->set_attribute("id", (boost::format("databank-%1%") % nr).str());
    result->set_attribute("parser", "generic");

    zx::element* dbs = mConfig.find_first("/mrs-config/databanks");
    dbs->append(result);

    return result;
}

void Reload()
{
    unique_ptr<File> newFile(new File);
    sInstance.reset(newFile.release());
}

fs::path GetDbDirectory(const std::string& inDatabankID)
{
    fs::path mrsdir(GetDirectory("mrs"));
    return mrsdir / (inDatabankID + ".m6");
}

void GetSchedule(bool& outEnabled, boost::posix_time::ptime& outTime, string& outWeekDay)
{
    zeep::xml::element* schedule = File::Instance().GetSchedule();
    if (schedule == nullptr)
        outEnabled = false;
    else
    {
        using namespace boost::gregorian;
        using namespace boost::posix_time;

        outEnabled = schedule->get_attribute("enabled") == "true";

        string updateTime = schedule->get_attribute("time");

        boost::regex rx("(\\d\\d):(\\d\\d)");
        boost::smatch m;
        if (not boost::regex_match(updateTime, m, rx))
            THROW(("Invalid time in schedule configuration"));

        int updateHour = boost::lexical_cast<int>(m[1].str());
        int updateMinute = boost::lexical_cast<int>(m[2].str());

        outTime = ptime(second_clock::local_time().date()) + hours(updateHour) + minutes(updateMinute);
        outWeekDay = schedule->get_attribute("weekday");
    }
}

}
