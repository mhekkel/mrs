#include "M6Lib.h"

#include <iostream>

#include <boost/thread.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>

#include "M6Progress.h"

using namespace std;

struct M6ProgressImpl
{
					M6ProgressImpl(int64 inMax)
						: mMax(inMax), mConsumed(0)
						, mThread(boost::bind(&M6ProgressImpl::Run, this)) {}

	void			Run();
	
	void			PrintProgress();
	void			PrintDone();

	int64			mMax, mConsumed;
	string			mMessage;
	boost::mutex	mMutex;
	boost::thread	mThread;
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
	int twd = static_cast<int>(tw * progress);
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

	string msg;
	msg.reserve(width + 1);
	msg += '\r';
	
	if (mMessage.length() < (width - 5))
		msg += mMessage;
	else
		msg += mMessage.substr(width - 8) + "...";

	cout << msg << endl;
}

M6Progress::M6Progress(int64 inMax)
	: mImpl(new M6ProgressImpl(inMax))
{
}

M6Progress::~M6Progress()
{
//	mImpl->mMutex.lock();
	mImpl->mThread.interrupt();
	mImpl->mThread.join();
	delete mImpl;
}
	
void M6Progress::Update(int64 inConsumed, const std::string& inMessage)
{
	boost::mutex::scoped_lock lock(mImpl->mMutex);
	mImpl->mConsumed += inConsumed;
	mImpl->mMessage = inMessage;

	if (mImpl->mConsumed == mImpl->mMax)
		mImpl->mThread.interrupt();
}

void M6Progress::Consumed(int64 inConsumed)
{
	boost::mutex::scoped_lock lock(mImpl->mMutex);
	mImpl->mConsumed += inConsumed;

	if (mImpl->mConsumed == mImpl->mMax)
		mImpl->mThread.interrupt();
}

void M6Progress::Progress(int64 inProgress)
{
	boost::mutex::scoped_lock lock(mImpl->mMutex);
	mImpl->mConsumed = inProgress;

	if (mImpl->mConsumed == mImpl->mMax)
		mImpl->mThread.interrupt();
}

void M6Progress::Message(const std::string& inMessage)
{
	boost::mutex::scoped_lock lock(mImpl->mMutex);
	mImpl->mMessage = inMessage;
}

