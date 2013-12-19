//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>

#include <log4cpp/Appender.hh>
#include <log4cpp/FileAppender.hh>
#include "log4cpp/PatternLayout.hh"

#include "M6Config.h"
#include "M6Log.h"

using namespace std;

namespace fs = boost::filesystem;

M6Logger::M6Logger() : mLog4cppLogger(log4cpp::Category::getRoot())
{
  // Creates a simple log4cpp logger.
  fs::path debugLogFile = fs::path(M6Config::GetDirectory("log")) / "debug.log";
  log4cpp::Appender *pAppender = new log4cpp::FileAppender("default", debugLogFile.string() );

  log4cpp::PatternLayout *layout = new log4cpp::PatternLayout();
  layout->setConversionPattern("%d{%Y-%m-%d %H:%M:%S} [%p] %c: %m%n");
  pAppender->setLayout(layout);
 
  mLog4cppLogger.setPriority(log4cpp::Priority::DEBUG);
  mLog4cppLogger.addAppender(pAppender);
}
 
M6Logger& M6Logger::GetLogger()
{
  static M6Logger logger;
  return logger;
}
 
void M6Logger::Log(M6LogLevel level, const std::string& format, ... )
{

  // Translate mrs logging level to log4cpp logging level
  log4cpp::Priority::PriorityLevel log4cppLevel = log4cpp::Priority::NOTSET;
  switch (level)
  {
  case NOTSET: // allow fall through
  case EMERG:  log4cppLevel = log4cpp::Priority::EMERG;  break;
  case ALERT:  log4cppLevel = log4cpp::Priority::ALERT;  break;
  case CRIT:   log4cppLevel = log4cpp::Priority::CRIT;   break;
  case ERROR:  log4cppLevel = log4cpp::Priority::ERROR;  break;
  case WARN:   log4cppLevel = log4cpp::Priority::WARN;   break;
  case NOTICE: log4cppLevel = log4cpp::Priority::NOTICE; break;
  case INFO:   log4cppLevel = log4cpp::Priority::INFO;   break;
  case DEBUG:  log4cppLevel = log4cpp::Priority::DEBUG;  break;
  default:          // LCOV_EXCL_LINE
    assert(false);  // LCOV_EXCL_LINE
  };
  assert(log4cppLevel != log4cpp::Priority::NOTSET);
 
  // Log message
  va_list args;                                                           
  va_start(args, format);
  mLog4cppLogger.logva(log4cppLevel, format.c_str(), args);
  va_end(args);
}
