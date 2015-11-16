//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <iostream>
#include <limits>

#include <boost/thread.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/regex.hpp>

#include "M6File.h"
#include "M6Error.h"

#if defined(_MSC_VER)
#include <Windows.h>
#endif

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

namespace M6IO
{

#if defined(_MSC_VER)

namespace
{

class MWinIOEvent
{
  public:
							~MWinIOEvent();

	static MWinIOEvent&		Instance();

	void					Wait();
							operator HANDLE()		{ return mEventHandle; }

  private:
							MWinIOEvent();

	HANDLE	mEventHandle;
};

MWinIOEvent::MWinIOEvent()
{
	mEventHandle = ::CreateEventA(nullptr, true, true, nullptr);
}

MWinIOEvent::~MWinIOEvent()
{
	::CloseHandle(mEventHandle);
}

MWinIOEvent& MWinIOEvent::Instance()
{
	static boost::thread_specific_ptr<MWinIOEvent> sIOEvent;
	if (sIOEvent.get() == nullptr)
		sIOEvent.reset(new MWinIOEvent);
	return *sIOEvent;
}

void MWinIOEvent::Wait()
{
	if (::WaitForSingleObject(mEventHandle, INFINITE) != WAIT_OBJECT_0)
		THROW(("Wait failed"));
}

}

// --------------------------------------------------------------------

M6Handle open(const std::string& inFile, MOpenMode inMode)
{
	unsigned long access = 0;
	unsigned long shareMode = 0;
	unsigned long create = 0;
	
	if (inMode == eReadWrite)
	{
		access = GENERIC_READ | GENERIC_WRITE;
		create = OPEN_ALWAYS;
	}
	else
	{
		access = GENERIC_READ;
		shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
		create = OPEN_EXISTING;
	}
	
	unsigned long flags = FILE_FLAG_OVERLAPPED;
	M6Handle result = reinterpret_cast<M6Handle>(::CreateFileA(inFile.c_str(),
		access, shareMode, nullptr, create, flags, nullptr));

	if (result < 0)
		THROW(("Error opening file %s, %d", inFile.c_str(), ::GetLastError()));
	
	return result;
}

void close(M6Handle inHandle)
{
	::CloseHandle(reinterpret_cast<HANDLE>(inHandle));
}

void truncate(M6Handle inHandle, int64 inSize)
{
	LARGE_INTEGER offset;
	offset.QuadPart = inSize;
	if (not ::SetFilePointerEx(reinterpret_cast<HANDLE>(inHandle), offset, nullptr, FILE_BEGIN))
		THROW(("Error seeking in file (truncate)"));
	if (not ::SetEndOfFile(reinterpret_cast<HANDLE>(inHandle)))
		THROW(("Error truncating file"));
}

int64 file_size(M6Handle inHandle)
{
	LARGE_INTEGER offset = { 0 };
	int64 size = 0;
	
	if (::SetFilePointerEx(reinterpret_cast<HANDLE>(inHandle), offset, &offset, FILE_END))
		size = offset.QuadPart;
	
	return size;
}

void pwrite(M6Handle inHandle, const void* inBuffer, int64 inSize, int64 inOffset)
{
	const char* buffer = reinterpret_cast<const char*>(inBuffer);
	
	while (inSize > 0)
	{
		OVERLAPPED p = {};
	
		p.hEvent = MWinIOEvent::Instance();
		p.Offset = static_cast<uint32>(inOffset);
		p.OffsetHigh = static_cast<uint32>(inOffset >> 32);
		
		int64 size = inSize;
		if (size > numeric_limits<int32>::max())
			size = numeric_limits<int32>::max();
	
		DWORD written = 0;
		if (not ::WriteFile(reinterpret_cast<HANDLE>(inHandle), inBuffer,
				static_cast<DWORD>(inSize), &written, &p) or written == 0)
		{
			if (::GetLastError() == ERROR_IO_PENDING)
			{
				MWinIOEvent::Instance().Wait();
				if (not ::GetOverlappedResult(reinterpret_cast<HANDLE>(inHandle), &p, &written, true) or (written == 0 and inSize > 0))
					THROW(("Error in PWrite (written == 0, inSize == %d)", inSize));
			}
			else
				THROW(("Error in PWrite (inSize == %d)", inSize));
		}
		
		inSize -= written;
		inOffset += written;
		buffer += written;
	}
}

void pread(M6Handle inHandle, void* inBuffer, int64 inSize, int64 inOffset)
{
	char* buffer = reinterpret_cast<char*>(inBuffer);

	// read in blocks of at most max_int32 size
	while (inSize > 0)
	{
		OVERLAPPED p = {};
	
		p.hEvent = MWinIOEvent::Instance();
		p.Offset = static_cast<uint32>(inOffset);
		p.OffsetHigh = static_cast<uint32>(inOffset >> 32);
		
		int64 size = inSize;
		if (size > numeric_limits<int32>::max())
			size = numeric_limits<int32>::max();
	
		DWORD read;
		if (not ::ReadFile(reinterpret_cast<HANDLE>(inHandle), inBuffer, static_cast<DWORD>(inSize), &read, &p) or read == 0)
		{
			DWORD err = ::GetLastError();
			if (err == ERROR_IO_PENDING)
			{
				MWinIOEvent::Instance().Wait();
				if (not ::GetOverlappedResult(reinterpret_cast<HANDLE>(inHandle), &p, &read, true))
					THROW(("Error in PRead"));
			}
			else if (err == ERROR_HANDLE_EOF)
				THROW(("Attempt to read past end of file"));
			else
				THROW(("Error in PRead"));
		}
		
		inSize -= read;
		inOffset += read;
		buffer += read;
	}

}

#else

#include <fcntl.h>

M6Handle open(const std::string& inFile, MOpenMode inMode)
{
	int mode;
	
	if (inMode == eReadWrite)
		mode = O_RDWR | O_CREAT;
	else
		mode = O_RDONLY;

	int fd = open(inFile.c_str(), mode, 0644);
	if (fd < 0)
		THROW(("Error opening file %s, %s", inFile.c_str(), strerror(errno)));
	
	return fd;
}

void close(M6Handle inHandle)
{
	::close(inHandle);
}

void truncate(M6Handle inHandle, int64 inSize)
{
	int err = ftruncate(inHandle, inSize);
	if (err < 0)
		THROW(("truncate error: %s", strerror(errno)));
}

int64 file_size(M6Handle inHandle)
{
	int64 size = lseek(inHandle, 0, SEEK_END);
	if (size < 0)
		THROW(("Error seeking file: %s", strerror(errno)));
	return size;
}

void pwrite(M6Handle inHandle, const void* inBuffer, int64 inSize, int64 inOffset)
{
//cout << "pwrite(" << inSize << ", " << inOffset << ')' << endl;
	int64 result = ::pwrite(inHandle, inBuffer, inSize, inOffset);
	if (result < 0)
		THROW(("Error writing file: %s", strerror(errno)));
}

void pread(M6Handle inHandle, void* inBuffer, int64 inSize, int64 inOffset)
{
//cout << "pread(" << inSize << ", " << inOffset << ')' << endl;
	int64 result = ::pread(inHandle, inBuffer, inSize, inOffset);
	if (result < 0)
		THROW(("Error reading file: %s", strerror(errno)));

//char byte = *static_cast<char*>(inBuffer);
//if (isprint(byte))
//	cout << "pread(" << inSize << ", " << inOffset << ", '" << byte << "')" << endl;
//else
//	cout << "pread(" << inSize << ", " << inOffset << ", " << int(byte) << ")" << endl;
}

#endif

}

// --------------------------------------------------------------------

M6File::M6File()
	: mImpl(new M6FileImpl)
{
	mImpl->mHandle = -1;
	mImpl->mSize = 0;
	mImpl->mOffset = 0;
	mImpl->mRefCount = 1;
}

M6File::M6File(const M6File& inFile)
	: mImpl(inFile.mImpl)
{
	++mImpl->mRefCount;
}

M6File::M6File(const std::string& inFile, MOpenMode inMode)
	: mImpl(new M6FileImpl)
{
	mImpl->mHandle = M6IO::open(inFile, inMode);
	mImpl->mSize = M6IO::file_size(mImpl->mHandle);
	mImpl->mOffset = 0;
	mImpl->mRefCount = 1;
}

M6File::M6File(const boost::filesystem::path& inFile, MOpenMode inMode)
	: mImpl(new M6FileImpl)
{
	mImpl->mHandle = M6IO::open(inFile.string(), inMode);
	mImpl->mSize = M6IO::file_size(mImpl->mHandle);
	mImpl->mOffset = 0;
	mImpl->mRefCount = 1;
}

M6File::~M6File()
{
	if (--mImpl->mRefCount == 0)
	{
		if (mImpl->mHandle >= 0)
			M6IO::close(mImpl->mHandle);
		delete mImpl;
	}
}

M6File& M6File::operator=(const M6File& inFile)
{
	if (this != &inFile)
	{
		if (--mImpl->mRefCount == 0)
		{
			if (mImpl->mHandle >= 0)
				M6IO::close(mImpl->mHandle);
			delete mImpl;
		}
		mImpl = inFile.mImpl;
		++mImpl->mRefCount;
	}
	
	return *this;
}

void M6File::Close()
{
	if (mImpl->mHandle >= 0)
		M6IO::close(mImpl->mHandle);
	mImpl->mHandle = -1;
}

void M6File::Read(void* inBuffer, int64 inSize)
{
	PRead(inBuffer, inSize, mImpl->mOffset);
	mImpl->mOffset += inSize;
}

void M6File::Write(const void* inBuffer, int64 inSize)
{
	PWrite(inBuffer, inSize, mImpl->mOffset);
	mImpl->mOffset += inSize;
}

void M6File::PRead(void* inBuffer, int64 inSize, int64 inOffset)
{
	M6IO::pread(mImpl->mHandle, inBuffer, inSize, inOffset);
}

void M6File::PWrite(const void* inBuffer, int64 inSize, int64 inOffset)
{
	M6IO::pwrite(mImpl->mHandle, inBuffer, inSize, inOffset);
	if (mImpl->mSize < inOffset + inSize)
		mImpl->mSize = inOffset + inSize;
}

void M6File::Truncate(int64 inSize)
{
	M6IO::truncate(mImpl->mHandle, inSize);
	mImpl->mSize = inSize;
	if (mImpl->mOffset > mImpl->mSize)
		mImpl->mOffset = mImpl->mSize;
}

int64 M6File::Seek(int64 inOffset, int inMode)
{
	switch (inMode)
	{
		case SEEK_SET:	mImpl->mOffset = inOffset; break;
		case SEEK_CUR:	mImpl->mOffset += inOffset; break;
		case SEEK_END:	mImpl->mOffset = mImpl->mSize + inOffset; break;
	}
	
	return mImpl->mOffset;
}

// --------------------------------------------------------------------

namespace {

bool Match(const char* inPattern, const char* inName)
{
	for (;;)
	{
		char op = *inPattern;

		switch (op)
		{
			case 0:
				return *inName == 0;
			case '*':
			{
				if (inPattern[1] == 0)
				{
					while (*inName)
					{
						if (*inName == '/' or *inName == '\\')
							return false;
						++inName;
					}
					return true;
				}
				else
				{
					while (*inName)
					{
						if (Match(inPattern + 1, inName))
							return true;
						++inName;
					}
					return false;
				}
			}
			case '?':
				if (*inName)
					return Match(inPattern + 1, inName + 1);
				else
					return false;
			default:
				if ((*inName == '/' and op == '\\') or
					(*inName == '\\' and op == '/') or
					tolower(*inName) == tolower(op))
				{
					++inName;
					++inPattern;
				}
				else
					return false;
				break;
		}
	}
}

void expand_group(const string& inPattern, vector<string>& outExpanded)
{
	static boost::regex rx("\\{([^{},]+,[^{}]*)\\}");
	
	boost::smatch m;
	if (boost::regex_search(inPattern, m, rx))
	{
		vector<string> options;
		
		string group = m[1].str();
		ba::split(options, group, ba::is_any_of(","));
		
		for (string& option : options)
		{
			vector<string> expanded;
			expand_group(m.prefix() + option + m.suffix(), expanded);
			outExpanded.insert(outExpanded.end(), expanded.begin(), expanded.end());
		}
	}
	else
		outExpanded.push_back(inPattern);
}

}

bool M6FilePathNameMatches(const fs::path& inPath, const string inGlobPattern)
{
	bool result = false;
	
	if (not inPath.empty())
	{
		vector<string> patterns;
		ba::split(patterns, inGlobPattern, ba::is_any_of(";"));
		
		vector<string> expandedpatterns;
		for_each(patterns.begin(), patterns.end(), [&expandedpatterns](string& pattern)
		{
			expand_group(pattern, expandedpatterns);
		});
		
		for (string& pat : expandedpatterns)
		{
			if (Match(pat.c_str(), inPath.string().c_str()))
			{
				result = true;
				break;
			}
		}
	}
	
	return result;	
}

