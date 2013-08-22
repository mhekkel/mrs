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
