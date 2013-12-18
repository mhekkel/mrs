//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)
//  Also distributed under the Lesser General Public License, Version 2.1.
//     (See accompanying file lgpl-2.1.txt or copy at
//           https://www.gnu.org/licenses/lgpl-2.1.txt)

#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>

#include <log4cpp/Appender.hh>
#include <log4cpp/FileAppender.hh>

#include "M6Config.h"
#include "M6Log.h"

using namespace std;

namespace fs = boost::filesystem;

void InitLogs()
{
	// init debug log:
	log4cpp::Category& debugLogger = log4cpp::Category::getInstance(std::string(LOG_DEBUG));

	fs::path debugLogFile = fs::path(M6Config::GetDirectory("log")) / "debug.log";

	log4cpp::Appender *debugAppender = new log4cpp::FileAppender("default", debugLogFile.string() );

	debugAppender->setLayout(new log4cpp::BasicLayout());

	debugLogger.setPriority(log4cpp::Priority::DEBUG);

	debugLogger.addAppender(debugAppender);
}
