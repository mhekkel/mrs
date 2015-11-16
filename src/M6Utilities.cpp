//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//	 (See accompanying file LICENSE_1_0.txt or copy at
//		   http://www.boost.org/LICENSE_1_0.txt)

// Utility routines, most are OS specific

#include "M6Lib.h"

#include <signal.h>
#include <fstream>
#include <iostream>

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/thread/condition_variable.hpp>

#include "M6Utilities.h"
#include "M6Error.h"
#include "M6Config.h"
#include "M6Log.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

#if defined(_MSC_VER)

#include <Windows.h>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

void SetStdinEcho(bool inEnable)
{
	HANDLE hStdin = ::GetStdHandle(STD_INPUT_HANDLE); 
	DWORD mode;
	::GetConsoleMode(hStdin, &mode);

	if(not inEnable)
		mode &= ~ENABLE_ECHO_INPUT;
	else
		mode |= ENABLE_ECHO_INPUT;

	::SetConsoleMode(hStdin, mode);
}

fs::path GetExecutablePath()
{
	WCHAR buffer[4096];
	
	DWORD n = ::GetModuleFileNameW(nullptr, buffer, sizeof(buffer) / sizeof(WCHAR));
	if (n == 0)
		throw runtime_error("could not get exe path");

	return fs::path(buffer);
}

bool IsPIDFileForExecutable(const fs::path& inPidFile)
{
	return false;
}

bool IsaTTY()
{
	return true;
}

// --------------------------------------------------------------------
// 
//	Daemonize
//	

void Daemonize(const string& inUser, const string& inPidFile)
{
	
}

int StopDaemon(int pid)
{
	return 0;
}

int KillDaemon(int pid, int sig)
{
	return 0;
}

// --------------------------------------------------------------------
// 
//	OpenLogFile
//	

void OpenLogFile(const string& inLogFile, const string& inErrFile)
{
	static ofstream outfile, errfile;
	
	if (outfile.is_open())
		outfile.close();
	outfile.open(inLogFile);
	if (not outfile.is_open())
		THROW(("Failed to create log file %s", inLogFile.c_str()));
	
	cout.rdbuf(outfile.rdbuf());
	
	if (errfile.is_open())
		errfile.close();
	errfile.open(inErrFile);
	if (not errfile.is_open())
		THROW(("Failed to create log file %s", inErrFile.c_str()));
	cerr.rdbuf(errfile.rdbuf());
}

// --------------------------------------------------------------------
// 
//	Signal handling
//	

struct M6SignalCatcherImpl
{
	static BOOL __stdcall   CtrlHandler(DWORD inCntrlType);

	static void				Signal(int inSignal);

	static int				sSignal;
	static boost::condition_variable
							sCondition;
	static boost::mutex		sMutex;
};

int M6SignalCatcherImpl::sSignal;
boost::condition_variable M6SignalCatcherImpl::sCondition;
boost::mutex M6SignalCatcherImpl::sMutex;

BOOL M6SignalCatcherImpl::CtrlHandler(DWORD inCntrlType)
{
	BOOL result = true;
	
	switch (inCntrlType) 
	{ 
		// Handle the CTRL-C signal. 
		case CTRL_C_EVENT: 
			sSignal = SIGINT;
			break;
		
		// CTRL-CLOSE: confirm that the user wants to exit. 
		case CTRL_CLOSE_EVENT:
			sSignal = SIGQUIT;
			break;
		
		// Pass other signals to the next handler. 
		case CTRL_BREAK_EVENT: 
			sSignal= SIGHUP;
			break;
		
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_LOGOFF_EVENT: 
			sSignal = SIGTERM;
			break;
		
		default: 
			result = FALSE;
	}
	
	if (result)
		sCondition.notify_one();
	
	return result;
}

M6SignalCatcher::M6SignalCatcher()
	: mImpl(nullptr)
{
	if (not ::SetConsoleCtrlHandler(&M6SignalCatcherImpl::CtrlHandler, true))
		THROW(("Could not install control handler"));
}

M6SignalCatcher::~M6SignalCatcher()
{
}

void M6SignalCatcher::BlockSignals()
{
}

void M6SignalCatcher::UnblockSignals()
{
}

int M6SignalCatcher::WaitForSignal()
{
	boost::unique_lock<boost::mutex> lock(M6SignalCatcherImpl::sMutex);
	M6SignalCatcherImpl::sCondition.wait(lock);
	return M6SignalCatcherImpl::sSignal;
}

void M6SignalCatcher::Signal(int inSignal)
{
	M6SignalCatcherImpl::CtrlHandler(CTRL_BREAK_EVENT);
}

#elif defined(linux) || defined(__linux) || defined (__linux__) || defined(__APPLE__) || defined(__FreeBSD__)

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <cerrno>
#include <pwd.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <termios.h>
#include <limits.h>
#else
#include <termio.h>
#include <linux/limits.h>
#endif
#include <fcntl.h>

namespace
{

string gExePath;
	
}

void SetStdinEcho(bool inEnable)
{
	struct termios tty;
	::tcgetattr(STDIN_FILENO, &tty);
	if(not inEnable)
		tty.c_lflag &= ~ECHO;
	else
		tty.c_lflag |= ECHO;

	(void)::tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

fs::path GetExecutablePath()
{
	if (gExePath.empty())
	{
		char path[PATH_MAX] = "";
		if (readlink("/proc/self/exe", path, sizeof(path)) == -1)
			THROW(("could not get exe path (%s)", strerror(errno)));
		
		gExePath = path;
	}
	
	return fs::path(gExePath);
}

bool IsPIDFileForExecutable(const fs::path& inPidFile)
{
	bool result = false;

	if (fs::exists(inPidFile))
	{
		fs::ifstream pidfile(inPidFile);
		if (not pidfile.is_open())
			THROW(("Failed to open pid file %s: %s", inPidFile.string().c_str(), strerror(errno)));
		
		int pid;
		pidfile >> pid;
		
		// if /proc/PID/exe points to our executable, this means we're already running
		char path[PATH_MAX] = "";
		if (readlink((boost::format("/proc/%1%/exe") % pid).str().c_str(), path, sizeof(path)) > 0)
		{
			string exe(GetExecutablePath().string());
	
			result = (exe == path) or
				(ba::ends_with(path, " (deleted)") and ba::starts_with(path, exe));

		} else if (errno == ENOENT) { // link file doesn't exist
			result = false;
		} else {
			THROW(("Failed to read executable link : %s", strerror(errno)));
		}
	}

	return result;
}

bool IsaTTY()
{
	return isatty(STDOUT_FILENO);
}

// --------------------------------------------------------------------
// 
//	Daemonize
//	

void Daemonize(const string& inUser, const string& inPidFile)
{
	// store exe path, we might not be able to access it later on
	char path[PATH_MAX] = "";
	if (readlink("/proc/self/exe", path, sizeof(path)) > 0)
		gExePath = path;

	int pid = fork();
	
	if (pid == -1)
	{
		cerr << "Fork failed" << endl;
		exit(1);
	}
	
	// exit the parent (=calling) process
	if (pid != 0)
		_exit(0);

	if (setsid() < 0)
	{
		cerr << "Failed to create process group: " << strerror(errno) << endl;
		exit(1);
	}

	// it is dubious if this is needed:
	signal(SIGHUP, SIG_IGN);

	// fork again, to avoid being able to attach to a terminal device
	pid = fork();

	if (pid == -1)
		cerr << "Fork failed" << endl;

	if (pid != 0)
		_exit(0);

	if (not inPidFile.empty())
	{
		// write our pid to the pid file
		ofstream pidFile(inPidFile);
		if(!pidFile.is_open())
		{
			cerr << "Failed to write to " << inPidFile << ": " << strerror(errno) << endl;
			exit(1);
		}
		pidFile << getpid() << endl;
		pidFile.close();
	}

	if (chdir("/") != 0)
	{
		cerr << "Cannot chdir to /: " << strerror(errno) << endl;
		exit(1);
	}

	if (inUser.length() > 0)
	{
		struct passwd* pw = getpwnam(inUser.c_str());
		if (pw == NULL or setuid(pw->pw_uid) < 0)
		{
			cerr << "Failed to set uid to " << inUser << ": " << strerror(errno) << endl;
			exit(1);
		}
	}

	// close stdin
	close(STDIN_FILENO);
	open("/dev/null", O_RDONLY);
}

int StopDaemon(int pid)
{
	int result = ::kill(pid, SIGINT);
	if (result!=0)
	{
		cerr << "Failed to stop process " << pid << ": " << strerror(errno) << endl;
	}
	return result;
}

int KillDaemon(int pid, int sig)
{
	return ::kill(pid, sig);
}

// --------------------------------------------------------------------
// 
//	OpenLogFile
//	

void OpenLogFile(const string& inLogFile, const string& inErrFile)
{
	// open the log file
	int fd = open(inLogFile.c_str(), O_CREAT|O_APPEND|O_RDWR, 0644);
	if (fd < 0)
	{
		cerr << "Opening log file " << inLogFile << " failed" << endl;
		exit(1);
	}

	// redirect stdout and stderr to the log file
	dup2(fd, STDOUT_FILENO);
	close(fd);

	// open the error file
	fd = open(inErrFile.c_str(), O_CREAT|O_APPEND|O_RDWR, 0644);
	if (fd < 0)
	{
		cerr << "Opening log file " << inErrFile << " failed" << endl;
		exit(1);
	}

	// redirect stdout and stderr to the log file
	dup2(fd, STDERR_FILENO);
	close(fd);
}

// --------------------------------------------------------------------
// 
//	Signal
//	

struct M6SignalCatcherImpl
{
	sigset_t new_mask, old_mask;
};

M6SignalCatcher::M6SignalCatcher()
	: mImpl(new M6SignalCatcherImpl)
{
	sigfillset(&mImpl->new_mask);
}

M6SignalCatcher::~M6SignalCatcher()
{
	delete mImpl;
}

void M6SignalCatcher::BlockSignals()
{
	pthread_sigmask(SIG_BLOCK, &mImpl->new_mask, &mImpl->old_mask);
}

void M6SignalCatcher::UnblockSignals()
{
	pthread_sigmask(SIG_SETMASK, &mImpl->old_mask, nullptr);
}

int M6SignalCatcher::WaitForSignal()
{
	// Wait for signal indicating time to shut down.
	sigset_t wait_mask;
	sigemptyset(&wait_mask);
	sigaddset(&wait_mask, SIGINT);
	sigaddset(&wait_mask, SIGHUP);
	sigaddset(&wait_mask, SIGCHLD);
	sigaddset(&wait_mask, SIGQUIT);
	sigaddset(&wait_mask, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &wait_mask, 0);

	int sig = 0;
	sigwait(&wait_mask, &sig);
	return sig;
}

void M6SignalCatcher::Signal(int inSignal)
{
	kill(getpid(), SIGHUP);
}

#else
#error "OS unknown"
#endif
