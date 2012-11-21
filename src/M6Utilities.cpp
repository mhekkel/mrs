// Utility routines, most are OS specific

#include "M6Lib.h"

#include <signal.h>

#include "M6Utilities.h"
#include "M6Error.h"

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

struct M6SignalCatcherImpl
{
	static BOOL				CtrlHandler(DWORD inCntrlType);

	static void				Signal(int inSignal);

	static int				sSignal;
	static boost::condition	sCondition;
	static boost::mutex		sMutex;
};

int M6SignalCatcherImpl::sSignal;
boost::condition M6SignalCatcherImpl::sCondition;
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
	boost::mutex::scoped_lock lock(M6SignalCatcherImpl::sMutex);
	M6SignalCatcherImpl::sCondition.wait(lock);
	return M6SignalCatcherImpl::sSignal;
}

void M6SignalCatcher::Signal(int inSignal)
{
	M6SignalCatcherImpl::CtrlHandler(CTRL_BREAK_EVENT);
}

#elif defined(linux) || defined(__linux) || defined (__linux__)

#include <termio.h>

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
