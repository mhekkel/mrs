//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <iostream>

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
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>

#include "M6Exec.h"
#include "M6Error.h"
#include "M6Server.h"
#include "M6Log.h"

using namespace std;
namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

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

int ForkExec(vector<const char*>& args, double maxRunTime, istream& in, ostream& out, ostream& err)
{
    if (args.empty() or args.front() == nullptr)
        THROW(("No arguments to ForkExec"));

    string cmd;
    for (auto arg = args.begin(); arg != args.end() and *arg != nullptr; ++arg)
        cmd = cmd + '"' + *arg + "\" ";

    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = true;

    HANDLE hInputWriteTmp, hInputRead, hInputWrite;
    HANDLE hOutputReadTmp, hOutputRead, hOutputWrite;
    HANDLE hErrorReadTmp, hErrorRead, hErrorWrite;

    if (not CreatePipe(&hOutputReadTmp, &hOutputWrite, &sa, 0) or
        not CreatePipe(&hErrorReadTmp, &hErrorWrite, &sa, 0) or
        not CreatePipe(&hInputRead, &hInputWriteTmp, &sa, 0) or

        not DuplicateHandle(GetCurrentProcess(), hOutputReadTmp,
                            GetCurrentProcess(), &hOutputRead,
                            0, false, DUPLICATE_SAME_ACCESS) or
        not DuplicateHandle(GetCurrentProcess(), hErrorReadTmp,
                            GetCurrentProcess(), &hErrorRead,
                            0, false, DUPLICATE_SAME_ACCESS) or
        not DuplicateHandle(GetCurrentProcess(), hInputWriteTmp,
                            GetCurrentProcess(), &hInputWrite,
                            0, false, DUPLICATE_SAME_ACCESS) or

        not CloseHandle(hOutputReadTmp) or
        not CloseHandle(hErrorReadTmp) or
        not CloseHandle(hInputWriteTmp))
    {
        THROW(("Error creating pipes"));
    }

    boost::thread thread([&in, hInputWrite]() {
        char buffer[1024];

        while (not in.eof())
        {
            streamsize k = io::read(in, buffer, sizeof(buffer));

            if (k == -1)
                break;

            const char* b = buffer;

            while (k > 0)
            {
                DWORD w;
                if (not WriteFile(hInputWrite, buffer, k, &w, nullptr) or
                    w != k)
                {
                    cerr << "Error writing to pipe " << ::GetLastError() << endl;
                    return;
                }

                if (w > 0)
                    b += w, k -= w;
            }
        }

        CloseHandle(hInputWrite);
    });

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hInputRead;
    si.hStdOutput = hOutputWrite;
    si.hStdError = hErrorWrite;

    const char* cwd = nullptr;

    PROCESS_INFORMATION pi;
    bool running = CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr, nullptr, true,
        CREATE_NEW_PROCESS_GROUP, nullptr, const_cast<char*>(cwd), &si, &pi);

    CloseHandle(hOutputWrite);
    CloseHandle(hErrorWrite);
    CloseHandle(hInputRead);

    if (running)
    {
        CloseHandle(pi.hThread);

        bool outDone = false, errDone = false;
        bool outCR = false, errCR = false;

        while (not (outDone and errDone))
        {
            char buffer[1024];
            DWORD rr, avail;

            if (not outDone)
            {
                if (not PeekNamedPipe(hOutputRead, nullptr, 0, nullptr, &avail, nullptr))
                {
                    unsigned int err = GetLastError();
                    if (err == ERROR_HANDLE_EOF or err == ERROR_BROKEN_PIPE)
                        outDone = true;
                }
                else if (avail > 0 and ReadFile(hOutputRead, buffer, sizeof(buffer), &rr, nullptr))
                {
                    for (char* a = buffer, *b = buffer; a != buffer + rr; ++a, ++b)
                    {
                        if (*a == '\r')
                        {
                            *b = '\n';
                            outCR = true;
                        }
                        else
                        {
                            if (*a == '\n' and not outCR)
                                *b = *a;
                            outCR = false;
                        }
                    }

                    out.write(buffer, rr);
                }
            }

            if (not errDone)
            {
                if (not PeekNamedPipe(hErrorRead, nullptr, 0, nullptr, &avail, nullptr))
                {
                    unsigned int err = GetLastError();
                    if (err == ERROR_HANDLE_EOF or err == ERROR_BROKEN_PIPE)
                        errDone = true;
                }
                else if (avail > 0 and ReadFile(hErrorRead, buffer, sizeof(buffer), &rr, nullptr))
                {
                    for (char* a = buffer, *b = buffer; a != buffer + rr; ++a, ++b)
                    {
                        if (*a == '\r')
                        {
                            *b = '\n';
                            errCR = true;
                        }
                        else
                        {
                            if (*a == '\n' and not errCR)
                                *b = *a;
                            errCR = false;
                        }
                    }

                    err.write(buffer, rr);
                }
            }
        }

        CloseHandle(pi.hProcess);
    }

    CloseHandle(hOutputRead);
    CloseHandle(hErrorRead);

    return 0;
}

struct M6ProcessImpl
{
                    M6ProcessImpl(const string& inCommand, istream& inRawData);

    void            Reference();
    void            Release();

    streamsize        read(char* s, streamsize n);

    HANDLE            mOFD[2];

  private:
                    ~M6ProcessImpl();

    void            init();

    boost::thread    mThread;
    int32            mRefCount;
    bool            mRunning;
    HANDLE            mHOutputRead, mHProcess;
};

M6ProcessImpl::M6ProcessImpl(const string& inCommand, istream& inRawData)
    : mRefCount(1), mRunning(false), mHOutputRead(nullptr)
{
    mOFD[0] = mOFD[1] = nullptr;

    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = true;

    HANDLE hInputWriteTmp, hInputRead, hInputWrite;
    HANDLE hOutputReadTmp, hOutputRead, hOutputWrite;

    if (not CreatePipe(&hOutputReadTmp, &hOutputWrite, &sa, 0) or
        not CreatePipe(&hInputRead, &hInputWriteTmp, &sa, 0) or

        not DuplicateHandle(GetCurrentProcess(), hOutputReadTmp,
                            GetCurrentProcess(), &hOutputRead,
                            0, false, DUPLICATE_SAME_ACCESS) or
        not DuplicateHandle(GetCurrentProcess(), hInputWriteTmp,
                            GetCurrentProcess(), &hInputWrite,
                            0, false, DUPLICATE_SAME_ACCESS) or

        not CloseHandle(hOutputReadTmp) or
        not CloseHandle(hInputWriteTmp))
    {
        THROW(("Error creating pipes"));
    }

    mThread = boost::thread([&inRawData, hInputWrite]() {
        char buffer[1024];

        while (not inRawData.eof())
        {
            streamsize k = io::read(inRawData, buffer, sizeof(buffer));

            if (k == -1)
                break;

            const char* b = buffer;

            while (k > 0)
            {
                DWORD w;
                if (not WriteFile(hInputWrite, buffer, k, &w, nullptr) or
                    w != k)
                {
                    cerr << "Error writing to pipe " << ::GetLastError() << endl;
                    return;    // what else
                }

                if (w > 0)
                    b += w, k -= w;
            }
        }

        if (hInputWrite != nullptr)
            CloseHandle(hInputWrite);
    });

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hInputRead;
    si.hStdOutput = hOutputWrite;
    si.hStdError = hOutputWrite;

    const char* cwd = nullptr;

    PROCESS_INFORMATION pi;
    mRunning = CreateProcessA(nullptr, const_cast<char*>(inCommand.c_str()), nullptr, nullptr, true,
        CREATE_NEW_PROCESS_GROUP, nullptr, const_cast<char*>(cwd), &si, &pi);

    CloseHandle(hOutputWrite);
    CloseHandle(hInputRead);

    if (mRunning)
    {
        CloseHandle(pi.hThread);
        mHProcess = pi.hProcess;
    }

    mHOutputRead = hOutputRead;
}

M6ProcessImpl::~M6ProcessImpl()
{
    if (mThread.joinable())
    {
        mThread.interrupt();
        mThread.join();
    }

    if (mHProcess != nullptr)
        CloseHandle(mHProcess);

    if (mHOutputRead != nullptr)
        CloseHandle(mHOutputRead);
}

void M6ProcessImpl::Reference()
{
    ++mRefCount;
}

void M6ProcessImpl::Release()
{
    if (--mRefCount == 0)
        delete this;
}

streamsize M6ProcessImpl::read(char* s, streamsize n)
{
    streamsize result = -1;

    while (mHOutputRead != nullptr and n > 0)
    {
        DWORD rr, avail;

        if (mHOutputRead)
        {
            if (not PeekNamedPipe(mHOutputRead, nullptr, 0, nullptr, &avail, nullptr))
            {
                unsigned int err = GetLastError();
                if (err == ERROR_HANDLE_EOF or err == ERROR_BROKEN_PIPE)
                {
                    CloseHandle(mHOutputRead);
                    mHOutputRead = nullptr;
                }
            }
            else if (avail > 0 and ReadFile(mHOutputRead, s, avail < n ? avail : n, &rr, nullptr))
                result += rr, n -= rr;
        }
    }

    return result;
}

#else

int ForkExec(vector<const char*>& args, double maxRunTime, istream& stdin, ostream& stdout, ostream& stderr)
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

    LOG(DEBUG, "ForkExec piping %s", args.front());

    err = pipe(ifd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
    err = pipe(ofd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
    err = pipe(efd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));

    boost::asio::io_service* ioService = nullptr;
    if (M6Server::Instance() != nullptr)
    {
        ioService = &M6Server::Instance()->get_io_service();
        ioService->notify_fork(boost::asio::io_service::fork_prepare);
    }

    int pid = fork();

    LOG(DEBUG, "running fork as process %d", pid);

    if (pid == 0)    // the child
    {
        LOG(DEBUG, "running as fork child");

        if (ioService != nullptr)
        {
            LOG(DEBUG, "notify fork child");

            ioService->notify_fork(boost::asio::io_service::fork_child);

            LOG(DEBUG, "stop server instance");

            /*
                Actually, network connections need to be closed here,
                but 'stop' hangs.
             */
            //M6Server::Instance()->stop();
        }

        LOG(DEBUG, "detaching fork child from process group");

        setpgid(0, 0);        // detach from the process group, create new

        signal(SIGCHLD, SIG_IGN);    // block child died signals

        dup2(ifd[0], STDIN_FILENO);
        close(ifd[0]);
        close(ifd[1]);

        dup2(ofd[1], STDOUT_FILENO);
        close(ofd[0]);
        close(ofd[1]);

        dup2(efd[1], STDERR_FILENO);
        close(efd[0]);
        close(efd[1]);

        LOG(DEBUG, "fork child executing args");

        const char* env[] = { nullptr };
        (void)execve(args.front(), const_cast<char* const*>(&args[0]), const_cast<char* const*>(env));

        LOG(DEBUG, "ending fork child");
        exit(-1);
    }

    if (ioService != nullptr)
        ioService->notify_fork(boost::asio::io_service::fork_parent);

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

    // handle stdin, if any
    close(ifd[0]);

    boost::thread thread([&stdin, ifd, args]()
    {
        char buffer[1024];

        LOG(DEBUG, "running ForkExec thread");

        while (not stdin.eof())
        {
            streamsize k = io::read(stdin, buffer, sizeof(buffer));

            if (k <= -1)
                break;

            const char* b = buffer;

            while (k > 0)
            {
                int r = write(ifd[1], b, k);
                if (r > 0)
                    b += r, k -= r;
                else if (r < 0 and errno != EAGAIN)
                    THROW(("Error writing to command %s", args.front()));
            }
        }

        close(ifd[1]);

        LOG(DEBUG, "ending ForkExec thread");
    });

    // make stdout and stderr non-blocking
    int flags;

    close(ofd[1]);
    flags = fcntl(ofd[0], F_GETFL, 0);
    fcntl(ofd[0], F_SETFL, flags | O_NONBLOCK);

    close(efd[1]);
    flags = fcntl(efd[0], F_GETFL, 0);
    fcntl(efd[0], F_SETFL, flags | O_NONBLOCK);

    // OK, so now the executable is started and the pipes are set up
    // read from the pipes until done.

    bool errDone = false, outDone = false, killed = false;

    LOG(DEBUG, "ForkExec reading pipes");

    while (not errDone and not outDone and not killed)
    {
        char buffer[1024];
        int r;

        while (not outDone)
        {
            r = read(ofd[0], buffer, sizeof(buffer));

            if (r > 0)
                stdout.write(buffer, r);
            else if (r == 0 or errno != EAGAIN)
                outDone = true;
            else
                break;
        }

        while (not errDone)
        {
            r = read(efd[0], buffer, sizeof(buffer));

            if (r > 0)
                stderr.write(buffer, r);
            else if (r == 0 and errno != EAGAIN)
                errDone = true;
            else
                break;
        }

        if (not errDone and not outDone)
        {
            if (not killed and maxRunTime > 0 and startTime + maxRunTime < system_time())
            {
                kill(pid, SIGKILL);
                killed = true;

                stderr << endl
                       << "maximum run time exceeded"
                       << endl;

                LOG(DEBUG, "maximum run time exceeded");
            }
            else
                sleep(1);
        }
    }

    LOG(DEBUG, "ForkExec joining thread");

    thread.join();

    close(ofd[0]);
    close(efd[0]);

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
                M6ProcessImpl(const string& inCommand, istream& inRawData);

    void        Reference();
    void        Release();

    streamsize    read(char* s, streamsize n);

  private:
                ~M6ProcessImpl();

    int32        mRefCount;
    int            mOFD;
    boost::thread
                mThread;
    string        mCommand;
    istream&    mRawData;
};

M6ProcessImpl::M6ProcessImpl(const string& inCommand, istream& inRawData)
    : mRefCount(1), mOFD(-1), mCommand(inCommand), mRawData(inRawData)
{
}

M6ProcessImpl::~M6ProcessImpl()
{
    assert(mRefCount == 0);
    if (mOFD != -1)
        ::close(mOFD);

    if (mThread.joinable())
    {
        mThread.interrupt();
        mThread.join();
    }
}

void M6ProcessImpl::Reference()
{
    ++mRefCount;
}

void M6ProcessImpl::Release()
{
    if (--mRefCount == 0)
        delete this;
}

streamsize M6ProcessImpl::read(char* s, streamsize n)
{
    if (mOFD == -1)
    {
        boost::mutex m;
        boost::condition c;
        boost::mutex::scoped_lock lock(m);

        mThread = boost::thread([this, &c]()
        {
            try
            {
                vector<string> argss = po::split_unix(this->mCommand);
                vector<const char*> args;
                for (string& arg : argss)
                    args.push_back(arg.c_str());

                if (args.empty() or args.front() == nullptr)
                    THROW(("No arguments to ForkExec"));

                if (args.back() != nullptr)
                    args.push_back(nullptr);

                if (not fs::exists(args.front()))
                    THROW(("The executable '%s' does not seem to exist", args.front()));

                if (VERBOSE)
                    cerr << "Starting executable " << args.front() << endl;

                // ready to roll
                int ifd[2], ofd[2], err;

                err = pipe(ifd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
                err = pipe(ofd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));

                int pid = fork();

                if (pid == -1)
                {
                    ::close(ifd[0]);
                    ::close(ifd[1]);
                    ::close(ofd[0]);
                    ::close(ofd[1]);

                    THROW(("fork failed: %s", strerror(errno)));
                }

                if (pid == 0)    // the child
                {
                    setpgid(0, 0);        // detach from the process group, create new

                    signal(SIGCHLD, SIG_IGN);    // block child died signals

                    dup2(ifd[0], STDIN_FILENO);
                    ::close(ifd[0]);
                    ::close(ifd[1]);

                    dup2(ofd[1], STDOUT_FILENO);
                    ::close(ofd[0]);
                    ::close(ofd[1]);

                    const char* env[] = { nullptr };
                    (void)execve(args.front(), const_cast<char* const*>(&args[0]), const_cast<char* const*>(env));
                    exit(-1);
                }

                ::close(ofd[1]);
                mOFD = ofd[0];

                c.notify_one();

                for (;;)
                {
                    char buffer[4096];

                    streamsize n = io::read(mRawData, buffer, sizeof(buffer));
                    if (n <= 0)
                    {
                        ::close(ifd[1]);
                        break;
                    }

                    char* b = buffer;
                    while (n > 0)
                    {
                        int r = ::write(ifd[1], b, n);
                        if (r == -1 and errno == EAGAIN)
                            continue;
                        if (r <= 0)
                            THROW(("WriteFile failed: %s", strerror(errno)));
                        n -= r;
                    }
                }

                if (pid > 0)
                {
                    int status = 0;
                    waitpid(pid, &status, 0);

//                    int result = -1;
//                    if (WIFEXITED(status))
//                        result = WEXITSTATUS(status);
                }
            }
            catch (exception& e)
            {
                cerr << "Process " << this->mCommand << " Failed: " << e.what() << endl;
                exit(1);
            }
        });

        c.wait(lock);
    }

    return ::read(mOFD, s, n);
}

#endif

M6Process::M6Process(const string& inCommand, istream& inRawData)
    : mImpl(new M6ProcessImpl(inCommand, inRawData))
{
}

M6Process::~M6Process()
{
    mImpl->Release();
}

M6Process::M6Process(const M6Process& rhs)
    : mImpl(rhs.mImpl)
{
    mImpl->Reference();
}

M6Process& M6Process::operator=(const M6Process& rhs)
{
    if (this != &rhs)
    {
        mImpl->Release();
        mImpl = rhs.mImpl;
        mImpl->Reference();
    }

    return *this;
}

streamsize M6Process::read(char* s, streamsize n)
{
    return mImpl->read(s, n);
}
