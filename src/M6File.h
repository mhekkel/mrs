//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

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

typedef int64	M6Handle;

namespace M6IO
{
	M6Handle open(const std::string& inFile, MOpenMode inMode);

	void close(M6Handle inHandle);
	void truncate(M6Handle inHandle, int64 inSize);
	int64 file_size(M6Handle inHandle);
	
	void pwrite(M6Handle inHandle, const void* inBuffer, int64 inSize, int64 inOffset);
	void pread(M6Handle inHandle, void* inBuffer, int64 inSize, int64 inOffset);
}

class M6FileReader;
class M6FileWriter;
struct M6FileSizeHelper;

bool M6FilePathNameMatches(const boost::filesystem::path& inPath, const std::string inGlobPattern);

class M6File
{
  public:
					M6File();
					M6File(const M6File& inFile);
					M6File(const std::string& inFile, MOpenMode inMode);
					M6File(const boost::filesystem::path& inFile, MOpenMode inMode);
	M6File&			operator=(const M6File& inFile);
	virtual			~M6File();
	
	void			Close();

	void			Read(void* inBuffer, int64 inSize);
	void			Write(const void* inBuffer, int64 inSize);
	
	void			PRead(void* inBuffer, int64 inSize, int64 inOffset);
	void			PWrite(const void* inBuffer, int64 inSize, int64 inOffset);
	
	template<class S>
	void			PRead(S& outStruct, int64 inOffset)
						{ PRead(&outStruct, sizeof(S), inOffset); }
	
	template<class S>
	void			PWrite(S& outStruct, int64 inOffset)
						{ PWrite(&outStruct, sizeof(S), inOffset); }
	
	virtual void	Truncate(int64 inSize);
	int64			Size() const						{ return mImpl->mSize; }
	int64			Seek(int64 inOffset, int inMode);
	int64			Tell() const						{ return mImpl->mOffset; }

	M6Handle		GetHandle() const					{ return mImpl->mHandle; }

  protected:

	struct M6FileImpl
	{
		M6Handle		mHandle;
		int64			mSize;
		int64			mOffset;
		uint32			mRefCount;
	};

	M6FileImpl*		mImpl;
};
