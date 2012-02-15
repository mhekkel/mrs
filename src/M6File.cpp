#include "M6Lib.h"

#include <iostream>
#include <limits>

#include <boost/thread.hpp>

#include "M6File.h"
#include "M6Error.h"

#if defined(_MSC_VER)
#include <Windows.h>
#endif

using namespace std;

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

MHandle open(const std::string& inFile, MOpenMode inMode)
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
	MHandle result = reinterpret_cast<MHandle>(::CreateFileA(inFile.c_str(),
		access, shareMode, nullptr, create, flags, nullptr));

	if (result < 0)
		THROW(("Error opening file %s, %d", inFile.c_str(), ::GetLastError()));
	
	return result;
}

MHandle open_tempfile(const std::string& inFileNameTemplate)
{
	THROW(("Unimplemented"));
	return MHandle(-1);
}
 
void close(MHandle inHandle)
{
	::CloseHandle(reinterpret_cast<HANDLE>(inHandle));
}

void truncate(MHandle inHandle, int64 inSize)
{
	LARGE_INTEGER offset;
	offset.QuadPart = inSize;
	if (not ::SetFilePointerEx(reinterpret_cast<HANDLE>(inHandle), offset, nullptr, FILE_BEGIN))
		THROW(("Error seeking in file (truncate)"));
	if (not ::SetEndOfFile(reinterpret_cast<HANDLE>(inHandle)))
		THROW(("Error truncating file"));
}

int64 file_size(MHandle inHandle)
{
	LARGE_INTEGER offset = { 0 };
	int64 size = 0;
	
	if (::SetFilePointerEx(reinterpret_cast<HANDLE>(inHandle), offset, &offset, FILE_END))
		size = offset.QuadPart;
	
	return size;
}

void pwrite(MHandle inHandle, const void* inBuffer, int64 inSize, int64 inOffset)
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

void pread(MHandle inHandle, void* inBuffer, int64 inSize, int64 inOffset)
{
//cout << "pread(" << inSize << ", " << inOffset << ')' << endl;

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

MHandle open(const std::string& inFile, MOpenMode inMode)
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

MHandle open_tempfile(const std::string& inFileNameTemplate)
{
//	char path[PATH_MAX] = {};
//
//	outPath = inDirectory / (inBaseName + ".XXXXXX");
//	strcpy(path, outPath.string().c_str());
//	
//	int fd = ::mkstemp(path);
//	if (fd < 0)
//		THROW(("Error creating temporary file: %s", strerror(errno)));
//
//	outPath = path;
//	
//	return fd;
	return -1;
}
 
void close(MHandle inHandle)
{
	::close(inHandle);
}

void truncate(MHandle inHandle, int64 inSize)
{
	int err = ftruncate(inHandle, inSize);
	if (err < 0)
		THROW(("truncate error: %s", strerror(errno)));
}

int64 file_size(MHandle inHandle)
{
	int64 size = lseek(inHandle, 0, SEEK_END);
	if (size < 0)
		THROW(("Error seeking file: %s", strerror(errno)));
	return size;
}

void pwrite(MHandle inHandle, const void* inBuffer, int64 inSize, int64 inOffset)
{
//cout << "pwrite(" << inSize << ", " << inOffset << ')' << endl;
	int64 result = ::pwrite(inHandle, inBuffer, inSize, inOffset);
	if (result < 0)
		THROW(("Error writing file: %s", strerror(errno)));
}

void pread(MHandle inHandle, void* inBuffer, int64 inSize, int64 inOffset)
{
//cout << "pread(" << inSize << ", " << inOffset << ')' << endl;
	int64 result = ::pread(inHandle, inBuffer, inSize, inOffset);
	if (result < 0)
		THROW(("Error reading file: %s", strerror(errno)));
}

#endif

}

// --------------------------------------------------------------------

M6File::M6File(const std::string& inFile, MOpenMode inMode)
	: mHandle(M6IO::open(inFile, inMode))
	, mSize(M6IO::file_size(mHandle))
{
}

M6File::~M6File()
{
	M6IO::close(mHandle);
}

void M6File::PRead(void* inBuffer, int64 inSize, int64 inOffset)
{
	M6IO::pread(mHandle, inBuffer, inSize, inOffset);
}

void M6File::PWrite(const void* inBuffer, int64 inSize, int64 inOffset)
{
	M6IO::pwrite(mHandle, inBuffer, inSize, inOffset);
	if (mSize < inOffset + inSize)
		mSize = inOffset + inSize;
}

void M6File::Truncate(int64 inSize)
{
	M6IO::truncate(mHandle, inSize);
	mSize = inSize;
}

// --------------------------------------------------------------------

M6FileStream::M6FileStream(const string& inFile, MOpenMode inMode)
	: M6File(inFile, inMode)
	, mOffset(0)
{
}

int64 M6FileStream::Seek(int64 inOffset, int inMode)
{
	switch (inMode)
	{
		case SEEK_SET:	mOffset = inOffset; break;
		case SEEK_CUR:	mOffset += inOffset; break;
		case SEEK_END:	mOffset = mSize + inOffset; break;
	}
	
	return mOffset;
}

int64 M6FileStream::Tell() const
{
	return mOffset;
}

void M6FileStream::Read(void* inBuffer, int64 inSize)
{
	PRead(inBuffer, inSize, mOffset);
	mOffset += inSize;
}

void M6FileStream::Write(const void* inBuffer, int64 inSize)
{
	PWrite(inBuffer, inSize, mOffset);
	mOffset += inSize;
}

void M6FileStream::Truncate(int64 inSize)
{
	M6File::Truncate(inSize);
	if (mOffset > mSize)
		mOffset = mSize;
}

