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

class M6FileReader;
class M6FileWriter;

class M6File
{
  public:
				M6File(const std::string& inFile, MOpenMode inMode);
	virtual		~M6File();
	
	void		PRead(void* inBuffer, int64 inSize, int64 inOffset);
	void		PWrite(const void* inBuffer, int64 inSize, int64 inOffset);
	
	template<class S>
	void		PRead(S& outStruct, int64 inOffset)
				{
					M6FileReader data(this, inOffset);
					outStruct.serialize(data);
				}
	
	template<class S>
	void		PWrite(S& outStruct, int64 inOffset)
				{
					M6FileWriter data(this, inOffset);
					outStruct.serialize(data);
				}
	
	void		Truncate(int64 inSize);
	int64		Size() const					{ return mSize; }

  private:
	MHandle		mHandle;
	int64		mSize;
};

#if 1 //LITTLE_ENDIAN

class M6FileReader
{
  public:
				M6FileReader(M6File* inFile, int64 inOffset)
					: mFile(inFile), mOffset(inOffset) {}

	template<class T>
	M6FileReader& operator&(T& v);

  private:
	M6File*		mFile;
	int64		mOffset;
};

template<>
inline
M6FileReader& M6FileReader::operator&<uint32>(uint32& v)
{
	mFile->PRead(&v, sizeof(v), mOffset);
	v = net_swapper::swap(v);
	mOffset += sizeof(v);
	return *this;
}

class M6FileWriter
{
  public:
				M6FileWriter(M6File* inFile, int64 inOffset)
					: mFile(inFile), mOffset(inOffset) {}

	template<class T>
	M6FileWriter& operator&(T v);

  private:
	M6File*		mFile;
	int64		mOffset;
};

template<>
inline
M6FileWriter& M6FileWriter::operator&<uint32>(uint32 v)
{
	v = net_swapper::swap(v);
	mFile->PWrite(&v, sizeof(v), mOffset);
	mOffset += sizeof(v);
	return *this;
}

#else

#endif
