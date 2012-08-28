#include "M6Lib.h"

#if defined(_MSC_VER)
#include <Windows.h>
#include <time.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#endif

#include <boost/filesystem/operations.hpp>

#include "M6Exec.h"
#include "M6Error.h"

using namespace std;

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
