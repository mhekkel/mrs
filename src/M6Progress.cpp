#include "M6Lib.h"

#include <iostream>
#include <functional>
#include <utility>

#if defined(__linux__)
#include <unistd.h>
#endif

#include <boost/thread.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#if BOOST_VERSION >= 104800
#include <boost/timer/timer.hpp>
#endif

#include "M6Utilities.h"
#include "M6Server.h"

#if defined(__linux__) || defined(__INTEL_COMPILER_BUILD_DATE)
#include <atomic>

typedef std::atomic<int64>	M6Counter;

inline int64 addCounter(M6Counter& ioCounter, int64 inIncrement)
{
	return ioCounter += inIncrement;
} 

inline int64 setCounter(M6Counter& ioCounter, int64 inValue)
{
	return ioCounter = inValue;
}

#else

#include <Windows.h>

typedef int64 M6Counter;

inline int64 addCounter(M6Counter& ioCounter, int64 inIncrement)
{
	return ::InterlockedExchangeAdd64(&ioCounter, inIncrement);
} 

inline int64 setCounter(M6Counter& ioCounter, int64 inValue)
{
	::InterlockedExchange64(&ioCounter, inValue);
	return inValue;
}


#endif

#include "M6Progress.h"

using namespace std;
namespace ip = boost::interprocess;

// --------------------------------------------------------------------

struct M6StatusImpl
{
	struct M6DbStatusInfo
	{
		char		databank[32];
		float		progress;			// negative value means error
		char		stage[220];
	};
	
	typedef ip::allocator<M6DbStatusInfo, ip::managed_shared_memory::segment_manager> M6ShmemAllocator;
	typedef ip::vector<M6DbStatusInfo, M6ShmemAllocator> M6DbSet;

				M6StatusImpl();
				~M6StatusImpl();

	bool		GetUpdateStatus(const string& inDatabank, string& outStage, float& outProgress);
	void		SetUpdateStatus(const string& inDatabank, const string& inStage, float inProgress);
	void		SetError(const string& inDatabank, const string& inErrorMessage);
	void		Cleanup(const string& inDatabank);

	ip::managed_shared_memory*	mSegment;
	M6ShmemAllocator*			mAllocator;
	M6DbSet*					mDbStatus;
};

BOOST_STATIC_ASSERT(sizeof(M6StatusImpl::M6DbStatusInfo) == 256);

M6StatusImpl::M6StatusImpl()
	: mSegment(nullptr), mAllocator(nullptr), mDbStatus(nullptr)
{
	try
	{
		mSegment = new ip::managed_shared_memory(ip::open_or_create, "M6SharedMemory", 65536);
		mAllocator = new M6ShmemAllocator(mSegment->get_segment_manager());
		
		mDbStatus = mSegment->find<M6DbSet>("M6DbSet").first;
		if (mDbStatus == nullptr)
			mDbStatus = mSegment->construct<M6DbSet>("M6DbSet")(*mAllocator);
	}
	catch (ip::interprocess_exception& e)
	{
		mDbStatus = nullptr;
		delete mAllocator;
		mAllocator = nullptr;
		delete mSegment;
		mSegment = nullptr;
	}
}

M6StatusImpl::~M6StatusImpl()
{
	delete mAllocator;
	delete mSegment;
}

bool M6StatusImpl::GetUpdateStatus(const string& inDatabank, string& outStage, float& outProgress)
{
	bool result = false;
	
	if (mDbStatus != nullptr)
	{
		foreach (const M6DbStatusInfo& info, *mDbStatus)
		{
			if (inDatabank != info.databank)
				continue;
	
			outStage = info.stage;
			outProgress = info.progress;
			result = true;
			break;
		}
	}
	
	return result;
}

void M6StatusImpl::SetUpdateStatus(const string& inDatabank, const string& inStage, float inProgress)
{
	// Believe it or not, but if you compile this code with -O3 using gcc
	// the copy of a float does not work...
	
	if (mDbStatus != nullptr)
	{
		M6DbSet::iterator i = mDbStatus->begin();
		while (i != mDbStatus->end() and inDatabank != i->databank)
			++i;
		
		if (i == mDbStatus->end())
		{
			M6DbStatusInfo info = {};
			strncpy(info.databank, inDatabank.c_str(), sizeof(info.databank) - 1);
			strncpy(info.stage, inStage.c_str(), sizeof(info.stage) - 1);
			info.progress = inProgress;
			
			i = mDbStatus->insert(i, info);
		}
		else
		{
			strncpy(i->stage, inStage.c_str(), sizeof(i->stage) - 1);
//			info.progress = inProgress;
			memcpy(&i->progress, &inProgress, sizeof(float));
		}
	}
}

void M6StatusImpl::SetError(const string& inDatabank, const string& inErrorMessage)
{
	SetUpdateStatus(inDatabank, inErrorMessage, -1.0f);
}

void M6StatusImpl::Cleanup(const string& inDatabank)
{
	if (mDbStatus != nullptr)
	{
		M6DbSet::iterator i = mDbStatus->begin();
		while (i != mDbStatus->end() and inDatabank != i->databank)
			++i;
		
		if (i != mDbStatus->end())
			mDbStatus->erase(i);
	}
}

M6Status& M6Status::Instance()
{
	static M6Status sInstance;
	return sInstance;
}

M6Status::M6Status()
	: mImpl(new M6StatusImpl)
{
}

M6Status::~M6Status()
{
	delete mImpl;
}

bool M6Status::GetUpdateStatus(const string& inDatabank, string& outStage, float& outProgress)
{
	bool result = false;
	if (mImpl != nullptr)
		result = mImpl->GetUpdateStatus(inDatabank, outStage, outProgress);
	return result;
}

void M6Status::SetUpdateStatus(const string& inDatabank, const string& inStage, float inProgress)
{
	if (mImpl != nullptr)
		mImpl->SetUpdateStatus(inDatabank, inStage, inProgress);
}

void M6Status::SetError(const string& inDatabank, const string& inErrorMessage)
{
	if (mImpl != nullptr)
		mImpl->SetError(inDatabank, inErrorMessage);
}

void M6Status::Cleanup(const string& inDatabank)
{
	if (mImpl != nullptr)
		mImpl->Cleanup(inDatabank);
}

// --------------------------------------------------------------------

struct M6ProgressImpl
{
					M6ProgressImpl(string inDatabank, int64 inMax, const string& inAction)
						: mDatabank(inDatabank)
						, mMax(inMax), mLast(0), mConsumed(0)
						, mAction(inAction), mMessage(inAction), mSpinner(0)
						, mThread(boost::bind(&M6ProgressImpl::Run, this)) {}
					~M6ProgressImpl();

	void			Run();
	
	void			PrintProgress();
	void			PrintDone();

	string			mDatabank;
	int64			mMax, mLast;
	M6Counter		mConsumed;
	string			mAction, mMessage;
	uint32			mSpinner;
	boost::mutex	mMutex;
	boost::thread	mThread;
#if BOOST_VERSION >= 104800
	boost::timer::cpu_timer
					mTimer;
#endif
};

M6ProgressImpl::~M6ProgressImpl()
{
	M6Status::Instance().Cleanup(mDatabank);
}

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
			
			if (mConsumed == mLast)
				continue;
			
			PrintProgress();
			mLast = mConsumed;
		}
	}
	catch (...) {}
	
	PrintDone();
}

void M6ProgressImpl::PrintProgress()
{
	int width = 80;
	float progress = -1.0f;
	
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

	if (mMax == numeric_limits<int64>::max())
	{
		const char kSpinner[] = { '|', '/', '-', '\\' };
		
		mSpinner = (mSpinner + 1) % 4;
		
		msg += ' ';
		msg += kSpinner[mSpinner];
	}
	else
	{
		msg += " [";
		
		progress = static_cast<float>(mConsumed) / mMax;
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
	}
	
	if (IsaTTY())
	{
		cout << '\r' << msg;
		cout.flush();
	}

	M6Status::Instance().SetUpdateStatus(mDatabank, mMessage, progress);
}

void M6ProgressImpl::PrintDone()
{
	int width = 80;

	string msg = mAction + " done";
#if BOOST_VERSION >= 104800
	msg += " in " + mTimer.format(0, "%ts cpu / %ws wall");
#endif
	if (msg.length() < width)
		msg += string(width - msg.length(), ' ');
	
	if (IsaTTY())
		cout << '\r' << msg << endl;
	else if (M6Server::Instance() == nullptr)
		cout << msg << endl;

	M6Status::Instance().SetUpdateStatus(mDatabank, mAction, 1.0f);
}

// --------------------------------------------------------------------

M6Progress::M6Progress(const string& inDatabank, int64 inMax, const string& inAction)
	: mImpl(new M6ProgressImpl(inDatabank, inMax, inAction))
{
}

M6Progress::M6Progress(const string& inDatabank, const string& inAction)
	: mImpl(new M6ProgressImpl(inDatabank, numeric_limits<int64>::max(), inAction))
{
}

M6Progress::~M6Progress()
{
	if (mImpl->mThread.joinable())
	{
		mImpl->mConsumed = mImpl->mMax;
		
		mImpl->mThread.interrupt();
		mImpl->mThread.join();
	}

	delete mImpl;
}
	
void M6Progress::Consumed(int64 inConsumed)
{
	if (addCounter(mImpl->mConsumed, inConsumed) >= mImpl->mMax and
		mImpl->mThread.joinable())
	{
		mImpl->mThread.interrupt();
		mImpl->mThread.join();
	}
}

void M6Progress::Progress(int64 inProgress)
{
	if (setCounter(mImpl->mConsumed, inProgress) >= mImpl->mMax and
		mImpl->mThread.joinable())
	{
		mImpl->mThread.interrupt();
		mImpl->mThread.join();
	}
}

void M6Progress::Message(const std::string& inMessage)
{
	boost::mutex::scoped_lock lock(mImpl->mMutex);
	mImpl->mMessage = inMessage;
}

