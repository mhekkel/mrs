//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <iostream>
#include <numeric>
#include <fstream>

#include <boost/iostreams/char_traits.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string.hpp>

#include "M6DataSource.h"
#include "M6Error.h"
#include "M6Progress.h"
#include "M6File.h"
#include "M6Exec.h"

using namespace std;
namespace fs = boost::filesystem;
namespace io = boost::iostreams;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------
// class compress_decompressor is based on code in libarchive, see below
// for more info on copyright and such.

class compress_decompressor
{
  public:
	typedef char							char_type;
	typedef io::multichar_input_filter_tag	category;

				compress_decompressor();

	template<typename Source>
	streamsize	read(Source& src, char* s, streamsize n);

  private:

	enum
	{
		errCompressOK = 0,
		errCompressEOF = -1,
		errCompressFatal = -2,
	};

	template<typename Source>
	void			init(Source& src);

	template<typename Source>
	int				getbits(Source& src, int n);

	template<typename Source>
	int				next_code(Source& src);

	bool			inited;
	
	/* Input variables. */
	int				bit_buffer;
	int				bits_avail;
	size_t			bytes_in_section;

	/* Decompression status variables. */
	int				use_reset_code;
	int				end_of_stream; /* EOF status. */
	int				maxcode;		/* Largest code. */
	int				maxcode_bits;	/* Length of largest code. */
	int				section_end_code; /* When to increase bits. */
	int				bits;			/* Current code length. */
	int				oldcode;		/* Previous code. */
	int				finbyte;		/* Last byte of prev code. */

	/* Dictionary. */
	int				free_ent;		 /* Next dictionary entry. */
	uint8			suffix[65536];
	uint16			prefix[65536];

	/*
	 * Scratch area for expanding dictionary entries.  Note:
	 * "worst" case here comes from compressing /dev/zero: the
	 * last code in the dictionary will code a sequence of
	 * 65536-256 zero bytes.  Thus, we need stack space to expand
	 * a 65280-byte dictionary entry.  (Of course, 32640:1
	 * compression could also be considered the "best" case. ;-)
	 */
	uint8*			stackp;
	uint8			stack[65300];
	
	// oooh...
	uint8			last_field;
};

// --------------------------------------------------------------------

struct M6DataSourceImpl
{
					M6DataSourceImpl(M6Progress& inProgress) : mProgress(inProgress) {}
	virtual			~M6DataSourceImpl() {}
	
	virtual M6DataSource::M6DataFile*
					Next() = 0;
	
	static M6DataSourceImpl*
					Create(const fs::path& inFile, M6Progress& inProgress);

	M6Progress&		mProgress;
};

// --------------------------------------------------------------------

M6DataSource::iterator::iterator(M6DataSourceImpl* inSource)
	: mSource(inSource), mDataFile(inSource->Next())
{
	if (mDataFile == nullptr)
		mSource = nullptr;
}

M6DataSource::iterator::iterator(const iterator& iter)
	: mSource(iter.mSource), mDataFile(iter.mDataFile)
{
	if (mDataFile != nullptr)
		++mDataFile->mRefCount;
}

M6DataSource::iterator&
M6DataSource::iterator::operator=(const iterator& iter)
{
	if (this != &iter)
	{
		if (mDataFile != nullptr and --mDataFile->mRefCount == 0)
			delete mDataFile;
		mSource = iter.mSource;
		mDataFile = iter.mDataFile;
		if (mDataFile != nullptr)
			++mDataFile->mRefCount;
	}
	return *this;
}

M6DataSource::iterator::~iterator()
{
	if (mDataFile != nullptr and --mDataFile->mRefCount == 0)
		delete mDataFile;
}

M6DataSource::iterator& M6DataSource::iterator::operator++()
{
	assert(mSource);
	if (mDataFile != nullptr and --mDataFile->mRefCount == 0)
		delete mDataFile;
	mDataFile = mSource->Next();
	if (mDataFile == nullptr)
		mSource = nullptr;
	return *this;
}

// --------------------------------------------------------------------

namespace
{

struct m6file_device : public io::source
{
	typedef char			char_type;
	typedef io::source_tag	category;

				m6file_device(const fs::path inFile, M6Progress& inProgress)
					: mFile(inFile, eReadOnly), mSize(fs::file_size(inFile)), mProgress(inProgress) {}

	streamsize	read(char* s, streamsize n)
				{
					if (n > mSize)
						n = mSize;
					if (n > 0)
					{
						mFile.Read(s, n);
						mProgress.Consumed(n);
						mSize -= n;
					}
					else
						n = -1;
					return n;
				}

	M6File		mFile;
	streamsize	mSize;
	M6Progress&	mProgress;
};

}

// --------------------------------------------------------------------

struct M6PlainTextDataSourceImpl : public M6DataSourceImpl
{
	typedef M6DataSource::M6DataFile M6DataFile;

							M6PlainTextDataSourceImpl(const fs::path& inFile, M6Progress& inProgress);

	virtual M6DataFile*		Next()	{ return mNext.release(); }

	unique_ptr<M6DataFile>	mNext;
};

M6PlainTextDataSourceImpl::M6PlainTextDataSourceImpl(const fs::path& inFile, M6Progress& inProgress)
	: M6DataSourceImpl(inProgress)
{
	if (not inFile.empty())
	{
		mNext.reset(new M6DataSource::M6DataFile);
		mNext->mFilename = inFile.filename().string();

		if (inFile.extension() == ".gz")
			mNext->mStream.push(io::gzip_decompressor());
		else if (inFile.extension() == ".bz2")
			mNext->mStream.push(io::bzip2_decompressor());
		else if (inFile.extension() == ".Z")
			mNext->mStream.push(compress_decompressor());
		
		mNext->mStream.push(m6file_device(inFile, mProgress));
		
		mProgress.Message(inFile.filename().string());
	}
}

// --------------------------------------------------------------------
// Read U Star archive files

struct M6TarDataSourceImpl : public M6DataSourceImpl
{
	typedef M6DataSource::M6DataFile M6DataFile;
	typedef M6DataSource::istream_type istream_type;
	
						M6TarDataSourceImpl(const fs::path& inArchive, M6Progress& inProgress);
						~M6TarDataSourceImpl();

	struct device : public io::source
	{
		typedef char			char_type;
		typedef io::source_tag	category;
	
						device(istream& inFile, streamsize inSize, M6Progress& inProgress)
							: mFile(inFile), mSize(inSize), mProgress(inProgress)
						{
							mTail = 512 - (inSize % 512);
							if (mTail == 512)
								mTail = 0;
						}

		streamsize		read(char* s, streamsize n)
						{
							streamsize result = -1;
							
							if (mSize > 0)
							{
								if (n > mSize)
									n = mSize;
								
								result = io::read(mFile, s, n);
								mSize -= result;
								
								if (mSize == 0)
								{
									char block[512];
									io::read(mFile, block, mTail);
								}
								
							}
							
							return result;
						}
	
		istream&		mFile;
		streamsize		mSize, mTail;
		M6Progress&		mProgress;
	};

	virtual M6DataFile*	Next();

	istream_type	mStream;
	M6Progress&		mProgress;
};

M6TarDataSourceImpl::M6TarDataSourceImpl(const fs::path& inArchive, M6Progress& inProgress)
	: M6DataSourceImpl(inProgress), mProgress(inProgress)
{
	if (inArchive.extension() == ".gz" or inArchive.extension() == ".tgz")
		mStream.push(io::gzip_decompressor());
	else if (inArchive.extension() == ".bz2" or inArchive.extension() == ".tbz")
		mStream.push(io::bzip2_decompressor());
	else if (inArchive.extension() == ".Z")
		mStream.push(compress_decompressor());
	
	mStream.push(m6file_device(inArchive, inProgress));
}

M6TarDataSourceImpl::~M6TarDataSourceImpl()
{
}

M6TarDataSourceImpl::M6DataFile* M6TarDataSourceImpl::Next()
{
	M6DataFile* result = nullptr;

	if (not mStream.eof())
	{
		uint32 nullBlocks = 0;
		
		for (;;)
		{
			char block[512];
			uint8* ublock = reinterpret_cast<uint8*>(block);
	
			streamsize n = io::read(mStream, block, sizeof(block));
			if (n < sizeof(block))
				THROW(("Error reading tar archive, premature end of file detected"));
			
			// check checksum
			uint32 cksum = 0;
			for (int i = 148, j = 0; i < 148 + 8; ++i)
			{
				if (block[i] == ' ') continue;
				if (block[i] == 0)
					block[i] = ' ';
				else if (block[i] < '0' or block[i] > '7')
					THROW(("Invalid checksum"));
				else
				{
					cksum = cksum << 3 | (ublock[i] - '0');
					block[i] = ' ';
					++j;
				}
			}
			
			if (cksum == 0 and accumulate(ublock, ublock + 512, 0UL) == 8 * ' ')
			{
				if (++nullBlocks == 2)
					break;
				continue;
			}
			
			int32 scksum = accumulate(block, block + 512, 0L);
			uint32 ucksum = accumulate(ublock, ublock + 512, 0UL);
			
			if (scksum != cksum and ucksum != cksum)
				THROW(("Checksum invalid"));
			
			int64 fileSize = 0;
			for (int i = 124; i < 124 + 12 - 1; ++i)
				fileSize = fileSize << 3 | (ublock[i] - '0');
			
			string filename(block);
			
			bool skip = false;

			if (strncmp(block + 257, "ustar", 5) == 0)	// ustar format
			{
				skip = block[156] != '0';

				if (block[345] != 0)
					filename = string(block + 345) + filename;
			}
			
			if (skip)
			{
				while (fileSize > 0)
				{
					n = io::read(mStream, block, sizeof(block));
					if (n != sizeof(block))
						THROW(("Premature end of file"));
					fileSize -= n;
				}
				continue;
			}

			result = new M6DataFile;
			result->mFilename = fs::path(filename).filename().string();
			result->mStream.push(device(mStream, fileSize, mProgress));
			
			mProgress.Message(result->mFilename);

			break;
		}
	}
	
	return result;
}

// --------------------------------------------------------------------

M6DataSourceImpl* M6DataSourceImpl::Create(const fs::path& inFile, M6Progress& inProgress)
{
	M6DataSourceImpl* result = nullptr;

	if (fs::file_size(inFile) != 0)
	{
		string name = inFile.filename().string();
				
		if (ba::ends_with(name, ".tar") or 
			ba::ends_with(name, ".tgz") or 
			ba::ends_with(name, ".tbz") or 
			ba::ends_with(name, ".tar.gz") or 
			ba::ends_with(name, ".tar.bz2") or
			ba::ends_with(name, ".tar.Z"))
		{
			result = new M6TarDataSourceImpl(inFile, inProgress);
		}
		else
			result = new M6PlainTextDataSourceImpl(inFile, inProgress);
	}
	
	return result;
}

// --------------------------------------------------------------------

M6DataSource::M6DataSource(const fs::path& inFile, M6Progress& inProgress)
	: mImpl(M6DataSourceImpl::Create(inFile, inProgress))
{
}

M6DataSource::~M6DataSource()
{
	delete mImpl;
}

#include "M6Decompress.ipp"
