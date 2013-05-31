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

// --------------------------------------------------------------------

// This code comes partly from libarchive

/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code borrows heavily from "compress" source code, which is
 * protected by the following copyright.  (Clause 3 dropped by request
 * of the Regents.)
 */

/*-
 * Copyright (c) 1985, 1986, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Diomidis Spinellis and James A. Woods, derived from original
 * work by Spencer Thomas and Joseph Orost.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

compress_decompressor::compress_decompressor()
	: inited(false), end_of_stream(false)
{
}

template<typename Source>
void compress_decompressor::init(Source& src)
{
	bits_avail = 0;
	bit_buffer = 0;

	int code = getbits(src, 8);
	if (code != 037) /* This should be impossible. */
		THROW(("Invalid compress data"));

	code = getbits(src, 8);
	if (code != 0235)
		THROW(("Invalid compress data"));

	code = getbits(src, 8);
	maxcode_bits = code & 0x1f;
	maxcode = (1 << maxcode_bits);
	use_reset_code = code & 0x80;

		/* Initialize decompressor. */
	free_ent = 256;
	stackp = stack;
	if (use_reset_code)
		free_ent++;
	bits = 9;
	section_end_code = (1 << bits) - 1;
	oldcode = -1;
	for (code = 255; code >= 0; code--)
	{
		prefix[code] = 0;
		suffix[code] = code;
	}
	next_code(src);
	
	inited = true;
}

template<typename Source>
streamsize compress_decompressor::read(Source& src, char* s, streamsize n)
{
	if (not inited)
		init(src);
	
	streamsize result = 0;

	while (n > 0 and not end_of_stream)
	{
		if (stackp > stack)
		{
			char ch = static_cast<char>(*--stackp);

			*s++ = ch;
			++result;
			--n;
			continue;
		}
		
		int ret = next_code(src);
		if (ret == errCompressEOF)
			end_of_stream = true;
		else if (ret != errCompressOK)
			THROW(("Decompression error"));
	}
	
	if (result == 0 and end_of_stream)
		result = -1;
	
    return result;
}

template<typename Source>
int compress_decompressor::getbits(Source& src, int n)
{
	int code;
	static const int mask[] = {
		0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff,
		0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff
	};

	while (bits_avail < n)
	{
		int c = io::get(src);
		
		if (c == EOF or c == io::WOULD_BLOCK)
			return errCompressEOF;
			
		bit_buffer |= c << bits_avail;
		bits_avail += 8;
		bytes_in_section++;
	}

	code = bit_buffer;
	bit_buffer >>= n;
	bits_avail -= n;

	return (code & mask[n]);
}

template<typename Source>
int compress_decompressor::next_code(Source& src)
{
	int code, newcode;

	code = newcode = getbits(src, bits);
	if (code < 0)
		return (code);

	/* If it's a reset code, reset the dictionary. */
	if ((code == 256) and use_reset_code)
	{
		/*
		 * The original 'compress' implementation blocked its
		 * I/O in a manner that resulted in junk bytes being
		 * inserted after every reset.  The next section skips
		 * this junk.  (Yes, the number of *bytes* to skip is
		 * a function of the current *bit* length.)
		 */
		int skip_bytes = bits - (bytes_in_section % bits);
		skip_bytes %= bits;
		bits_avail = 0; /* Discard rest of this byte. */
		while (skip_bytes-- > 0) 
		{
			code = getbits(src, 8);
			if (code < 0)
				return (code);
		}
		/* Now, actually do the reset. */
		bytes_in_section = 0;
		bits = 9;
		section_end_code = (1 << bits) - 1;
		free_ent = 257;
		oldcode = -1;
		return (next_code(src));
	}

	if (code > free_ent)
		THROW(("Invalid compressed data"));

	/* Special case for KwKwK string. */
	if (code >= free_ent)
	{
		*stackp++ = finbyte;
		code = oldcode;
	}

	/* Generate output characters in reverse order. */
	while (code >= 256)
	{
		*stackp++ = suffix[code];
		code = prefix[code];
	}
	*stackp++ = finbyte = code;

	/* Generate the new entry. */
	code = free_ent;
	if (code < maxcode && oldcode >= 0)
	{
		prefix[code] = oldcode;
		suffix[code] = finbyte;
		++free_ent;
	}
	if (free_ent > section_end_code)
	{
		bits++;
		bytes_in_section = 0;
		if (bits == maxcode_bits)
			section_end_code = maxcode;
		else
			section_end_code = (1 << bits) - 1;
	}

	/* Remember previous code. */
	oldcode = newcode;
	return errCompressOK;
}
