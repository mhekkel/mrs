#include "M6Lib.h"

#include <iostream>

#include <boost/thread.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/timer/timer.hpp>

#include "M6Progress.h"

using namespace std;

#if defined(_MSC_VER)
#include <Windows.h>

#define AtomicExchange(v,n)			InterlockedExchange64(v, n)
#define AtomicExchangeAndAdd(v,i)	InterlockedExchangeAdd64(v, i)

#elif defined(__linux__) || defined(__linux)

#define AtomicExchange(v,n)			atomic_exchange(v, n)
#define AtomicExchangeAndAdd(v,i)	atomic_exchange_and_add(v, i)

#endif

struct M6ProgressImpl
{
					M6ProgressImpl(int64 inMax, const string& inAction)
						: mMax(inMax), mConsumed(0), mAction(inAction), mMessage(inAction)
						, mThread(boost::bind(&M6ProgressImpl::Run, this)) {}

	void			Run();
	
	void			PrintProgress();
	void			PrintDone();

	int64			mMax, mConsumed;
	string			mAction, mMessage;
	boost::mutex	mMutex;
	boost::thread	mThread;
	boost::timer::cpu_timer
					mTimer;
};

void M6ProgressImpl::Run()
{
	try
	{
		for (;;)
		{
			boost::this_thread::sleep(boost::posix_time::seconds(1));
			
			boost::mutex::scoped_lock lock(mMutex);
			
			if (mConsumed == mMax)
				break;
			
			PrintProgress();
		}
	}
	catch (...) {}
	
	PrintDone();
}

void M6ProgressImpl::PrintProgress()
{
	int width = 80;
	
	string msg;
	msg.reserve(width + 1);
	if (mMessage.length() <= 20)
	{
		msg = mMessage;
		if (msg.length() < 20)
			msg.append(20 - msg.length(), ' ');
	}
	else
		msg = mMessage.substr(0, 17) + "...";
	
	msg += " [";
	
	float progress = static_cast<float>(mConsumed) / mMax;
	int tw = width - 28;
	int twd = static_cast<int>(tw * progress + 0.5f);
	msg.append(twd, '=');
	msg.append(tw - twd, ' ');
	msg.append("] ");
	
	int perc = static_cast<int>(100 * progress);
	if (perc < 100)
		msg += ' ';
	if (perc < 10)
		msg += ' ';
	msg += boost::lexical_cast<string>(perc);
	msg += '%';
	
	cout << '\r' << msg;
	cout.flush();
}

void M6ProgressImpl::PrintDone()
{
	int width = 80;

	string msg = mAction + " done in " + mTimer.format(0, "%ts cpu / %ws wall");
	if (msg.length() < width)
		msg += string(width - msg.length(), ' ');
	
	cout << '\r' << msg << endl;
}

M6Progress::M6Progress(int64 inMax, const string& inAction)
	: mImpl(new M6ProgressImpl(inMax, inAction))
{
}

M6Progress::~M6Progress()
{
//	mImpl->mMutex.lock();
	mImpl->mThread.interrupt();
	mImpl->mThread.join();
	delete mImpl;
}
	
void M6Progress::Consumed(int64 inConsumed)
{
	int64 old = AtomicExchangeAndAdd(&mImpl->mConsumed, inConsumed);

	if (old + inConsumed >= mImpl->mMax)
		mImpl->mThread.interrupt();
}

void M6Progress::Progress(int64 inProgress)
{
	int64 old = AtomicExchange(&mImpl->mConsumed, inProgress);

	if (inProgress >= mImpl->mMax)
		mImpl->mThread.interrupt();
}

void M6Progress::Message(const std::string& inMessage)
{
	boost::mutex::scoped_lock lock(mImpl->mMutex);
	mImpl->mMessage = inMessage;
}

