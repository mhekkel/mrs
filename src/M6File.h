#pragma once

#include <string>

// wrappers for doing low level file i/o.
// pread and pwrite will throw exceptions if they cannot read/write all requested data.

enum MOpenMode
{
	eReadOnly,
	eReadWrite
};

typedef int64	MHandle;

namespace M6IO
{

MHandle open(const std::string& inFile, MOpenMode inMode);
MHandle open_tempfile(const std::string& inFileNameTemplate); 
void close(MHandle inHandle);
void truncate(MHandle inHandle, int64 inSize);
int64 file_size(MHandle inHandle);

void pwrite(MHandle inHandle, const void* inBuffer, int64 inSize, int64 inOffset);
void pread(MHandle inHandle, void* inBuffer, int64 inSize, int64 inOffset);

}

class M6File
{
  public:
				M6File(const std::string& inFile, MOpenMode inMode);
	virtual		~M6File();
	
	void		PRead(void* inBuffer, int64 inSize, int64 inOffset);
	void		PWrite(const void* inBuffer, int64 inSize, int64 inOffset);

	void		Truncate(int64 inSize);
	int64		Size() const					{ return mSize; }

  private:
	MHandle		mHandle;
	int64		mSize;
};
