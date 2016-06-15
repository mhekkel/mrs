//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <boost/filesystem/fstream.hpp>
#include <boost/thread.hpp>

#define LOG(level, msg,...) M6Logger::GetLogger().Log(level, msg, ##__VA_ARGS__)

/// Logging levels used by mrs. Follows the same as for syslog, taken from
/// RFC 5424. Comments added for ease of reading.
/// @see http://en.wikipedia.org/wiki/Syslog.
enum M6LogLevel
{
  EMERG,  // System is unusable (e.g. multiple parts down)
  ALERT,  // System is unusable (e.g. single part down)
  CRIT,   // Failure in non-primary system (e.g. backup site down)
  ERROR,  // Non-urgent failures; relay to developers
  WARN,   // Not an error, but indicates error will occurr if nothing done.
  NOTICE, // Events that are unusual, but not error conditions.
  INFO,   // Normal operational messages. No action required.
  DEBUG,  // Information useful during development for debugging.
  NOTSET
};

class M6Logger
{
  public:
    static M6Logger& GetLogger();

    void Log(M6LogLevel level, const std::string& msg, ...);

  private:
    M6Logger();
    M6Logger(const M6Logger&) =delete;
    M6Logger(M6Logger&&) =delete;
    M6Logger& operator=(const M6Logger&) =delete;
    M6Logger& operator=(M6Logger&&) =delete;

  private:
    boost::filesystem::path mLogFilePath;
    boost::mutex mutex_log;
    bool bEnabled;
    M6LogLevel mPriority;
};
