//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <zeep/xml/document.hpp>

#include "M6Lib.h"


namespace M6Config
{

class File
{
  public:
                             File();
                             File(const File& inOther);
                             ~File();

    static File&            Instance();

    void                    Validate();
    void                    WriteOut();

    zeep::xml::element*        GetDirectory(const std::string& inID);
    zeep::xml::element*        GetTool(const std::string& inID);
    zeep::xml::element*        GetUser(const std::string& inName, const std::string& inRealm);
    zeep::xml::element*        CreateUser(const std::string& inName, const std::string& inRealm);
    zeep::xml::element*        GetServer();
    zeep::xml::element_set    GetFormats();
    zeep::xml::element*        GetFormat(const std::string& inID);
    zeep::xml::element*        CreateFormat();
    zeep::xml::element_set    GetParsers();
    zeep::xml::element*        GetParser(const std::string& inID);
    zeep::xml::element_set    GetDatabanks();
    zeep::xml::element_set    GetDatabanks(const std::string& inID);
    zeep::xml::element*        GetEnabledDatabank(const std::string& inID);
    zeep::xml::element*        GetConfiguredDatabank(const std::string& inID);
    zeep::xml::element*        CreateDatabank();
    zeep::xml::element*        GetSchedule();
    zeep::xml::element*        GetLogger();

  private:
    zeep::xml::element_set    Find(const boost::format& inFmt);
    zeep::xml::element*        FindFirst(const boost::format& inFmt);

    std::istream*            LoadDTD(const std::string& inBase,
                                const std::string& inPublicID, const std::string& inSystemID);

    zeep::xml::document        mConfig;
};

void SetConfigFilePath(const boost::filesystem::path& inConfigFile);

// Prototypes

void                            Reload();
const zeep::xml::element*        GetLogger();
std::string                        GetDirectory(const std::string& inID);
std::string                        GetTool(const std::string& inID);
uint32                            GetMaxRunTime(const std::string& inID);
const zeep::xml::element*        GetUser(const std::string& inName, const std::string& inRealm);
const zeep::xml::element*        GetServer();
const zeep::xml::element_set    GetFormats();
const zeep::xml::element*        GetFormat(const std::string& inID);
const zeep::xml::element_set    GetParsers();
const zeep::xml::element*        GetParser(const std::string& inID);
const zeep::xml::element_set    GetDatabanks();
const zeep::xml::element*        GetDatabank(const std::string& inID);
const zeep::xml::element_set    GetDatabanks(const std::string& inID);
std::string                        GetDatabankParam(const std::string& inID, const std::string& inParam);
boost::filesystem::path            GetDbDirectory(const std::string& inDatabankID);
void                            GetSchedule(bool& outEnabled, boost::posix_time::ptime& outTime,
                                    std::string& outWeekDay);

// Inlines

inline const zeep::xml::element* GetLogger()
{
    return File::Instance().GetLogger();
}

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

inline uint32 GetMaxRunTime(const std::string& inID)
{
    uint32 result = 0;
    zeep::xml::element* e = File::Instance().GetTool(inID);
    if (e != nullptr and not e->get_attribute("max-run-time").empty())
        result = atoi(e->get_attribute("max-run-time").c_str());
    return result;
}

inline const zeep::xml::element* GetUser(const std::string& inName, const std::string& inRealm)
{
    return File::Instance().GetUser(inName, inRealm);
}

inline const zeep::xml::element* GetServer()
{
    return File::Instance().GetServer();
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

inline const zeep::xml::element_set    GetDatabanks(const std::string& inID)
{
    return File::Instance().GetDatabanks(inID);
}

inline const zeep::xml::element* GetEnabledDatabank(const std::string& inID)
{
    return File::Instance().GetEnabledDatabank(inID);
}

inline const zeep::xml::element* GetConfiguredDatabank(const std::string& inID)
{
    return File::Instance().GetConfiguredDatabank(inID);
}

inline std::string GetDatabankParam(const std::string& inID, const std::string& inParam)
{
    std::string result;
    zeep::xml::element* db = File::Instance().GetEnabledDatabank(inID);
    if (db != nullptr)
    {
        zeep::xml::node* n = db->find_first_node(inParam.c_str());
        if (n != nullptr)
            result = n->str();
    }
    return result;
}

}
