#pragma once

#include <string>

#include <boost/type_traits/is_integral.hpp>

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

  protected:
	MHandle		mHandle;
	int64		mSize;
};

// M6FileStream is an extension of M6File having the notion of an offset

class M6FileStream : public M6File
{
  public:
				M6FileStream(const std::string& inFile, MOpenMode inMode);
	
	int64		Seek(int64 inOffset, int inMode);
	int64		Tell() const;
	
	void		Read(void* inBuffer, int64 inSize);
	void		Write(const void* inBuffer, int64 inSize);

  private:
	int64		mOffset;
};

class M6FileReader
{
  public:

	template<class T, bool>
	struct read_and_swap
	{
		void operator()(M6File& inFile, T& value, int64 inOffset)
		{
			inFile.PRead(value, sizeof(value), inOffset);
		}
	};

	template<class T>
	struct read_and_swap<T, true>
	{
		void operator()(M6File& inFile, T& value, int64 inOffset)
		{
			inFile.PRead(&value, sizeof(value), inOffset);
			value = swap_bytes(value);
		}
	};
	
				M6FileReader(M6File* inFile, int64 inOffset)
					: mFile(inFile), mOffset(inOffset) {}

	template<class T>
	M6FileReader& operator&(T& v)
	{
		read_and_swap<T, boost::is_integral<T>::value> read;
		read(*mFile, v, mOffset);
		mOffset += sizeof(v);
		return *this;
	}

  private:
	M6File*		mFile;
	int64		mOffset;
};

class M6FileWriter
{
  public:
				M6FileWriter(M6File* inFile, int64 inOffset)
					: mFile(inFile), mOffset(inOffset) {}

	template<class T, bool>
	struct write_and_swap
	{
		void operator()(M6File& inFile, const T& value, int64 inOffset)
		{
			inFile.PWrite(value, sizeof(value), inOffset);
		}
	};

	template<class T>
	struct write_and_swap<T, true>
	{
		void operator()(M6File& inFile, const T& value, int64 inOffset)
		{
			T v = swap_bytes(value);
			inFile.PWrite(&v, sizeof(T), inOffset);
		}
	};
	
	template<class T>
	M6FileWriter& operator&(const T& v)
	{
		write_and_swap<T, boost::is_integral<T>::value> write;
		write(*mFile, v, mOffset);
		mOffset += sizeof(v);
		return *this;
	}


  private:
	M6File*		mFile;
	int64		mOffset;
};
