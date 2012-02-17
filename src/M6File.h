#pragma once

#include <string>
#include <vector>

#include <boost/type_traits/is_integral.hpp>
#include <boost/filesystem/path.hpp>

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

// M6File class for doing I/O using the above functions.
// The class can use serialization for structures and then it byte-swaps
// all integers, effectively writing big-endian files.
// To use this:
//
//	struct foo {
//		int a;
//		long b;
//		template<class Archive>
//		void serialize(Archive& ar)
//		{
//			ar & a & b;
//		}
//	};
//
//	M6File file;
//	foo s;
//	file.PRead(s, 0);		// reads struct of type foo in s from offset 0
//

class M6FileReader;
class M6FileWriter;
struct M6FileSizeHelper;

class M6File
{
  public:
					M6File();
					M6File(const M6File& inFile);
					M6File(const std::string& inFile, MOpenMode inMode);
					M6File(const boost::filesystem::path& inFile, MOpenMode inMode);
	M6File&			operator=(const M6File& inFile);
	virtual			~M6File();

	void			Read(void* inBuffer, int64 inSize);
	void			Write(const void* inBuffer, int64 inSize);
	
	void			PRead(void* inBuffer, int64 inSize, int64 inOffset);
	void			PWrite(const void* inBuffer, int64 inSize, int64 inOffset);
	
	template<class S>
	void			PRead(S& outStruct, int64 inOffset)
					{
						M6FileSizeHelper size;
						outStruct.serialize(size);
						
						std::vector<uint8> buffer(size.mSize);
						PRead(&buffer[0], size.mSize, inOffset);
						
						M6FileReader data(&buffer[0]);
						outStruct.serialize(data);
					}
	
	template<class S>
	void			PWrite(S& outStruct, int64 inOffset)
					{
						M6FileSizeHelper size;
						outStruct.serialize(size);
						
						std::vector<uint8> buffer(size.mSize);

						M6FileWriter data(&buffer[0]);
						outStruct.serialize(data);

						PWrite(&buffer[0], size.mSize, inOffset);
					}
	
	virtual void	Truncate(int64 inSize);
	int64			Size() const						{ return mImpl->mSize; }
	int64			Seek(int64 inOffset, int inMode);
	int64			Tell() const						{ return mImpl->mOffset; }

  protected:

	struct M6FileImpl
	{
		MHandle			mHandle;
		int64			mSize;
		int64			mOffset;
		uint32			mRefCount;
	};

	M6FileImpl*		mImpl;
};

// helper classes

struct M6FileSizeHelper
{
					M6FileSizeHelper() : mSize(0)	{}
	
	template<class T>
	M6FileSizeHelper& operator&(const T& inValue)	{ mSize += sizeof(T); return *this; }

	int64			mSize;
};

class M6FileReader
{
  public:
					M6FileReader(const uint8* inBuffer) : mBuffer(inBuffer) {}

	template<class T, bool>
	struct reader
	{
		size_t operator()(const uint8* inBuffer, T& outValue) const
		{
			memcpy(&outValue, inBuffer, sizeof(T)); return sizeof(T);
		}
	};

	template<class T>
	struct reader<T, true>
	{
		size_t operator()(const uint8* inBuffer, T& outValue) const
		{
			for (int i = 0; i < sizeof(T); ++i)
				outValue = (outValue << 8) | *inBuffer++; 
			return sizeof(T);
		}
	};

	template<class T>
	M6FileReader&	operator&(T& v)
					{
						reader<T, boost::is_integral<T>::value> reader;
						mBuffer += reader(mBuffer, v);
						return *this;
					}

  private:
	const uint8*	mBuffer;
};

class M6FileWriter
{
  public:
					M6FileWriter(uint8* inBuffer) : mBuffer(inBuffer) {}

	template<class T, bool>
	struct writer
	{
		size_t operator()(uint8* inBuffer, const T& inValue) const
		{
			memcpy(inBuffer, &inValue, sizeof(T)); return sizeof(T);
		}
	};

	template<class T>
	struct writer<T, true>
	{
		size_t operator()(uint8* inBuffer, T inValue) const
		{
			for (int i = sizeof(T) - 1; i >= 0; --i)
			{
				inBuffer[i] = static_cast<uint8>(inValue);
				inValue >>= 8;
			}
			return sizeof(T);
		}
	};

	template<class T>
	M6FileWriter&	operator&(T& v)
					{
						writer<T, boost::is_integral<T>::value> writer;
						mBuffer += writer(mBuffer, v);
						return *this;
					}

	M6FileWriter&	operator&(int8 v)
					{
						*mBuffer++ = v;
						return *this;
					}

	M6FileWriter&	operator&(uint8 v)
					{
						*mBuffer++ = v;
						return *this;
					}

  private:
	uint8*			mBuffer;
};
