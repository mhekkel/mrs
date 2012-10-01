#include "M6Lib.h"

#if defined(_MSC_VER)
#include <Windows.h>
#include <time.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#endif

#include <boost/filesystem/operations.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Exec.h"
#include "M6Error.h"

using namespace std;
namespace fs = boost::filesystem;

double system_time()
{
#if defined(_MSC_VER)
	static double sDiff = -1.0;

	FILETIME tm;
	ULARGE_INTEGER li;
	
	if (sDiff == -1.0)
	{
		SYSTEMTIME st = { 0 };

		st.wDay = 1;
		st.wMonth = 1;
		st.wYear = 1970;

		if (not ::SystemTimeToFileTime(&st, &tm))
			THROW(("Getting system tile failed"));

		li.LowPart = tm.dwLowDateTime;
		li.HighPart = tm.dwHighDateTime;
		
		// Prevent Ping Pong comment. VC cannot convert UNSIGNED int64 to double. SIGNED is ok. (No more long)
		sDiff = static_cast<double> (li.QuadPart);
		sDiff /= 1e7;
	}	
	
	::GetSystemTimeAsFileTime(&tm);
	
	li.LowPart = tm.dwLowDateTime;
	li.HighPart = tm.dwHighDateTime;
	
	double result = static_cast<double> (li.QuadPart);
	result /= 1e7;
	return result - sDiff;
#else
	struct timeval tv;
	
	gettimeofday(&tv, nullptr);
	
	return tv.tv_sec + tv.tv_usec / 1e6;
#endif
}

#if defined(_MSC_VER)

int ForkExec(vector<const char*>& args, double maxRunTime,
	const string& in, string& out, string& err)
{
	THROW(("ForkExec not implemented yet"));
}

struct M6ProcessImpl
{
				M6ProcessImpl(const vector<const char*>& args);
				~M6ProcessImpl();

    bool		filter(const char*& begin_in, const char* end_in,
	                 char*& begin_out, char* end_out, bool flush);
    void		close();
  
	string		mCommand;
	bool		mEof;
	HANDLE		mProc, mThread;

	HANDLE		mIFD[2], mOFD[2], mEFD[2];
};

M6ProcessImpl::M6ProcessImpl(const vector<const char*>& args)
	: mEof(false), mProc(nullptr), mThread(nullptr)
{
	foreach (const char* arg, args)
	{
		if (mCommand.empty() == false)
			mCommand += ' ';
		mCommand += arg;
	}
	
	mIFD[0] = mIFD[1] = mOFD[0] = mOFD[1] = mEFD[0] = mEFD[1] = nullptr;
}

M6ProcessImpl::~M6ProcessImpl()
{
	close();
}

bool M6ProcessImpl::filter(const char*& begin_in, const char* end_in, char*& begin_out, char* end_out, bool flush)
{
	if (mProc == nullptr and mEof == false)
	{
		SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES) };
		sa.bInheritHandle = true;
	
		::CreatePipe(&mOFD[0], &mOFD[1], &sa, 0);
		::SetHandleInformation(mOFD[0], HANDLE_FLAG_INHERIT, 0); 
	
		::CreatePipe(&mEFD[0], &mEFD[1], &sa, 0);
		::SetHandleInformation(mEFD[0], HANDLE_FLAG_INHERIT, 0); 
	
		::CreatePipe(&mIFD[0], &mIFD[1], &sa, 0);
		::SetHandleInformation(mIFD[1], HANDLE_FLAG_INHERIT, 0);
		DWORD mode = PIPE_NOWAIT;
		::SetNamedPipeHandleState(mIFD[1], &mode, nullptr, nullptr);
	
		STARTUPINFOA si = { sizeof(STARTUPINFOA) };
		si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
		si.hStdInput = mIFD[0];
		si.hStdOutput = mOFD[1];
		si.hStdError = mEFD[1];
	
		PROCESS_INFORMATION pi;
		::CreateProcessA(nullptr, const_cast<char*>(mCommand.c_str()), nullptr, nullptr, true,
			CREATE_NEW_PROCESS_GROUP, nullptr, /*const_cast<char*>(cwd.c_str())*/ nullptr, &si, &pi);
	
		mProc = pi.hProcess;
		mThread = pi.hThread;
	}

	if (end_in > begin_in)
	{
		DWORD rr;
		if (not ::WriteFile(mIFD[1], begin_in, static_cast<DWORD>(end_in - begin_in), &rr, nullptr))
			THROW(("Error writing to command: %d", GetLastError()));
		begin_in += rr;
	}
	
	if (flush and begin_in == end_in)
	{
		::CloseHandle(mIFD[1]);
		mIFD[1] = nullptr;
	}
	
	if (mEFD[0] != nullptr)
	{
		DWORD avail, rr;
		if (not ::PeekNamedPipe(mEFD[0], nullptr, 0, nullptr, &avail, nullptr))
		{
			unsigned int err = ::GetLastError();
			if (err == ERROR_HANDLE_EOF or err == ERROR_BROKEN_PIPE)
			{
				::CloseHandle(mEFD[0]);
				mEFD[0] = nullptr;
			}
		}
		else if (avail > 0)
		{
			vector<char> buffer(avail);
			
			if (::ReadFile(mEFD[0], &buffer[0], avail, &rr, nullptr))
				cerr.write(&buffer[0], rr);
			cerr << endl;
		}
	}

	if (mOFD[0] != nullptr)
	{
		DWORD avail, rr;
		if (not ::PeekNamedPipe(mOFD[0], nullptr, 0, nullptr, &avail, nullptr))
		{
			unsigned int err = ::GetLastError();
			if (err == ERROR_HANDLE_EOF or err == ERROR_BROKEN_PIPE)
			{
				::CloseHandle(mOFD[0]);
				mOFD[0] = nullptr;
			}
		}
		else if (avail > 0)
		{
			if (not ::ReadFile(mOFD[0], begin_out, end_out - begin_out, &rr, nullptr))
				THROW(("Error reading data: %d", GetLastError()));
			begin_out += rr;
		}
	}

	return mEFD[0] != nullptr and mOFD[0] != nullptr;
}

void M6ProcessImpl::close()
{
	if (mIFD[1] != nullptr)
		::CloseHandle(mIFD[1]);
	if (mOFD[0] != nullptr)
		::CloseHandle(mOFD[0]);
	if (mEFD[0] != nullptr)
		::CloseHandle(mEFD[0]);
	
	if (mProc != nullptr)
	{
		::CloseHandle(mProc);
		::CloseHandle(mThread);
		mProc = mThread = nullptr;
	}
	
	mEof = false;
}

#else

int ForkExec(vector<const char*>& args, double maxRunTime,
	const string& stdin, string& stdout, string& stderr)
{
	if (args.empty() or args.front() == nullptr)
		THROW(("No arguments to ForkExec"));

	if (args.back() != nullptr)
		args.push_back(nullptr);
	
	if (not fs::exists(args.front()))
		THROW(("The executable '%s' does not seem to exist", args.front()));

	// ready to roll
	double startTime = system_time();

	int ifd[2], ofd[2], efd[2], err;
	
	err = pipe(ifd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
	err = pipe(ofd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
	err = pipe(efd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
	
	int pid = fork();
	
	if (pid == -1)
	{
		close(ifd[0]);
		close(ifd[1]);
		close(ofd[0]);
		close(ofd[1]);
		close(efd[0]);
		close(efd[1]);
		
		THROW(("fork failed: %s", strerror(errno)));
	}
	
	if (pid == 0)	// the child
	{
		setpgid(0, 0);		// detach from the process group, create new

		signal(SIGCHLD, SIG_IGN);	// block child died signals

		dup2(ifd[0], STDIN_FILENO);
		close(ifd[0]);
		close(ifd[1]);

		dup2(ofd[1], STDOUT_FILENO);
		close(ofd[0]);
		close(ofd[1]);
						
		dup2(efd[1], STDERR_FILENO);
		close(efd[0]);
		close(efd[1]);

		const char* env[] = { nullptr };
		(void)execve(args.front(), const_cast<char* const*>(&args[0]), const_cast<char* const*>(env));
		exit(-1);
	}
	
	// make stdout and stderr non-blocking
	int flags;
	
	close(ifd[0]);
	
	if (stdin.empty())
		close(ifd[1]);
	else
	{
		flags = fcntl(ifd[1], F_GETFL, 0);
		fcntl(ifd[1], F_SETFL, flags | O_NONBLOCK);
	}

	close(ofd[1]);
	flags = fcntl(ofd[0], F_GETFL, 0);
	fcntl(ofd[0], F_SETFL, flags | O_NONBLOCK);

	close(efd[1]);
	flags = fcntl(efd[0], F_GETFL, 0);
	fcntl(efd[0], F_SETFL, flags | O_NONBLOCK);
	
	// OK, so now the executable is started and the pipes are set up
	// read from the pipes until done.
	
	bool errDone = false, outDone = false, killed = false;
	
	const char* in = stdin.c_str();
	size_t l_in = stdin.length();
	
	while (not errDone and not outDone)
	{
		if (l_in > 0)
		{
			size_t k = l_in;
			if (k > 1024)
				k = 1024;
			
			int r = write(ifd[1], in, k);
			if (r > 0)
				in += r, l_in -= r;
			else if (r < 0 and errno != EAGAIN)
				THROW(("Error writing to command %s", args.front()));
			
			if (l_in == 0)
				close(ifd[1]);
		}
		else
			usleep(100000);

		char buffer[1024];
		int r, n;
		
		n = 0;
		while (not outDone)
		{
			r = read(ofd[0], buffer, sizeof(buffer));
			
			if (r > 0)
				stdout.append(buffer, r);
			else if (r == 0 or errno != EAGAIN)
				outDone = true;
			else
				break;
		}
		
		n = 0;
		while (not errDone)
		{
			r = read(efd[0], buffer, sizeof(buffer));
			
			if (r > 0)
				stderr.append(buffer, r);
			else if (r == 0 and errno != EAGAIN)
				errDone = true;
			else
				break;
		}

		if (not errDone and not outDone and not killed and startTime + maxRunTime < system_time())
		{
			kill(pid, SIGINT);

			int status = 0;
			waitpid(pid, &status, 0);

			THROW(("%s was killed since its runtime exceeded the limit of %d seconds", args.front(), maxRunTime));
		}
	}
	
	close(ofd[0]);
	close(efd[0]);
	if (l_in > 0)
		close(ifd[1]);
	
	// no zombies please, removed the WNOHANG. the forked application should really stop here.
	int status = 0;
	waitpid(pid, &status, 0);
	
	int result = -1;
	if (WIFEXITED(status))
		result = WEXITSTATUS(status);
	
	return result;
}

struct M6ProcessImpl
{
				M6ProcessImpl(const vector<const char*>& args);
				~M6ProcessImpl();

    bool		filter(const char*& begin_in, const char* end_in,
	                 char*& begin_out, char* end_out, bool flush);
    void		close();
  
	vector<const char*>
				mArgs;
	bool		mEof;
	int			mPID, mIFD, mOFD, mEFD;
};

M6ProcessImpl::M6ProcessImpl(const vector<const char*>& args)
	: mArgs(args), mEof(false), mPID(-1)
{
}

M6ProcessImpl::~M6ProcessImpl()
{
	close();
}

bool M6ProcessImpl::filter(const char*& begin_in, const char* end_in, char*& begin_out, char* end_out, bool flush)
{
	if (mPID == -1)
	{
		if (mArgs.empty() or mArgs.front() == nullptr)
			THROW(("No arguments to ForkExec"));
	
		if (mArgs.back() != nullptr)
			mArgs.push_back(nullptr);
		
		if (not fs::exists(mArgs.front()))
			THROW(("The executable '%s' does not seem to exist", mArgs.front()));
	
		// ready to roll
		double startTime = system_time();
	
		int ifd[2], ofd[2], efd[2], err;
		
		err = pipe(ifd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
		err = pipe(ofd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
		err = pipe(efd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
		
		int pid = fork();
		
		if (pid == -1)
		{
			close(ifd[0]);
			close(ifd[1]);
			close(ofd[0]);
			close(ofd[1]);
			close(efd[0]);
			close(efd[1]);
			
			THROW(("fork failed: %s", strerror(errno)));
		}
		
		if (pid == 0)	// the child
		{
			setpgid(0, 0);		// detach from the process group, create new
	
			signal(SIGCHLD, SIG_IGN);	// block child died signals
	
			dup2(ifd[0], STDIN_FILENO);
			close(ifd[0]);
			close(ifd[1]);
	
			dup2(ofd[1], STDOUT_FILENO);
			close(ofd[0]);
			close(ofd[1]);
							
			dup2(efd[1], STDERR_FILENO);
			close(efd[0]);
			close(efd[1]);
	
			const char* env[] = { nullptr };
			(void)execve(mArgs.front(), const_cast<char* const*>(&mArgs[0]), const_cast<char* const*>(env));
			exit(-1);
		}

		// make stdout and stderr non-blocking
		int flags;
		
		close(ifd[0]);
		flags = fcntl(ifd[1], F_GETFL, 0);
		fcntl(ifd[1], F_SETFL, flags | O_NONBLOCK);
	
		close(ofd[1]);
		flags = fcntl(ofd[0], F_GETFL, 0);
		fcntl(ofd[0], F_SETFL, flags | O_NONBLOCK);
		
		close(efd[1]);
		flags = fcntl(efd[0], F_GETFL, 0);
		fcntl(efd[0], F_SETFL, flags | O_NONBLOCK);
		
		mIFD = ifd[1];
		mOFD = ofd[0];
		mEFD = efd[0];
	}

	if (begin_in < end_in)
	{
		int r = write(mIFD, begin_in, end_in - begin_in);
		if (r > 0)
			begin_in += r;
		else if (r < 0 and errno != EAGAIN)
			THROW(("Error writing to command %s", mArgs.front()));
	}
	
	if (flush and begin_in == end_in)
	{
		close(mIFD);
		mIFD = -1;
	}
	
	if (mOFD >= 0)
	{
		int r = read(mOFD, begin_out, end_out - begin_out);
		
		if (r == 0 or errno != EAGAIN)
		{
			close(mOFD);
			mOFD = -1;
		}
	}

	if (mEFD >= 0)
	{
		char buffer[1024];
		
		int r = read(mEFD, buffer, sizeof(buffer));
		
		if (r == 0 or errno != EAGAIN)
		{
			close(mEFD);
			mEFD = -1;
		}
	}
	
	return (mEFD == -1 and mOFD == -1);
}

void M6ProcessImpl::close()
{
	if (mIFD >= 0)
		close(mIFD);
	if (mOFD >= 0)
		close(mOFD);
	if (mEFD >= 0)
		close(mEFD);
	
	mIFD = mOFD = mEFD = -1;
	
	if (mPID > 0)
	{
		int status = 0;
		waitpid(pid, &status, 0);
		
		int result = -1;
		if (WIFEXITED(status))
			result = WEXITSTATUS(status);
	}
}

#endif

M6Process::M6Process(const vector<const char*>& args)
	: mImpl(new M6ProcessImpl(args))
{
}

M6Process::~M6Process()
{
	delete mImpl;
}

bool M6Process::filter(const char*& begin_in, const char* end_in,
     char*& begin_out, char* end_out, bool flush)
{
	return mImpl->filter(begin_in, end_in, begin_out, end_out, flush);
}

void M6Process::close()
{
	mImpl->close();
}

