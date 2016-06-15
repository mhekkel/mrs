//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <string>
#include <cstdio>
#include <cstdarg>

#include "M6Config.h"
#include "M6Log.h"

using namespace std;

namespace fs = boost::filesystem;
namespace zx = zeep::xml;
namespace pt = boost::posix_time;

std::string M6LogLevel2String (const M6LogLevel level)
{
    switch (level)
    {
        case EMERG:  return "EMERG";
        case ALERT:  return "ALERT";
        case CRIT:   return "CRIT";
        case ERROR:  return "ERROR";
        case WARN:   return "WARN";
        case NOTICE: return "NOTICE";
        case INFO:   return "INFO";
        case DEBUG:  return "DEBUG";
        default:     return "NOTSET";
    };
}
M6LogLevel String2M6LogLevel (const std::string &s)
{
    if (s == "EMERG")
        return EMERG;
    else if (s == "ALERT")
        return ALERT;
    else if (s == "CRIT")
        return CRIT;
    else if (s == "ERROR")
        return ERROR;
    else if (s == "WARN")
        return WARN;
    else if (s == "NOTICE")
        return NOTICE;
    else if (s == "INFO")
        return INFO;
    else if (s == "DEBUG")
        return DEBUG;
    else
        return NOTSET;
}
M6Logger::M6Logger()
{
    // Creates a simple file logger.
    // logrotate.d/mrs will take care of rotating
    mLogFilePath = fs::path (M6Config::GetDirectory("log")) / "mrs.log";

    const zx::element *pLoggerConfig = M6Config::GetLogger();

    bEnabled = pLoggerConfig->get_attribute("enabled") == "true";

    if(bEnabled)
    {
        mPriority = String2M6LogLevel (pLoggerConfig->get_attribute("priority"));
    }
}

M6Logger& M6Logger::GetLogger()
{
    static M6Logger logger;
    return logger;
}

void M6Logger::Log (M6LogLevel level, const std::string& format, ... )
{
    if (!bEnabled || level == NOTSET || level > mPriority)
        return;

    // Lock and open file
    mutex_log.lock ();
    FILE *logFile = fopen (mLogFilePath.string ().c_str (), "a");

    if (logFile)
    {
        // current time and log level
        pt::time_facet *output_facet = new pt::time_facet ();
        output_facet->format ("%Y-%m-%d %H:%M:%S"); 
        std::ostringstream date_osstr;
        date_osstr.imbue (std::locale (date_osstr.getloc(), output_facet));
        date_osstr << pt::second_clock::local_time();

        fprintf (logFile, "%s [%s] : ",
                 date_osstr.str ().c_str (),
                 M6LogLevel2String (level).c_str ());

        // log message
        va_list args;
        va_start (args, format);
        vfprintf (logFile, format.c_str (), args);
        va_end (args);

        // end line and close
        fprintf (logFile, "\n");
        fclose (logFile);
    }

    // Unlock file
    mutex_log.unlock ();
}
