//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <signal.h>
#include <boost/filesystem/path.hpp>

#ifndef SIGQUIT
#define SIGQUIT SIGTERM
#endif

#ifndef SIGHUP
#define SIGHUP SIGBREAK
#endif

class M6SignalCatcher
{
  public:
				M6SignalCatcher();
				~M6SignalCatcher();

	void		BlockSignals();
	void		UnblockSignals();
	
	int			WaitForSignal();

	static void	Signal(int inSignal);

  private:
				M6SignalCatcher(const M6SignalCatcher&);
	M6SignalCatcher&
				operator=(const M6SignalCatcher&);

	struct M6SignalCatcherImpl*	mImpl;
};

void SetStdinEcho(bool inEnable);
boost::filesystem::path GetExecutablePath();
bool IsPIDFileForExecutable(const boost::filesystem::path& inFile);
bool IsaTTY();
void Daemonize(const std::string& inUser, const std::string& inPidFile);
int StopDaemon(int pid);
int KillDaemon(int pid, int sig);
void OpenLogFile(const std::string& inLogFile, const std::string& inErrFile);
