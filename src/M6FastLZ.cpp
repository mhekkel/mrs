/*
	The following code is based on fastlz written by Ariya Hidayat.
	I've only cleaned up the code a bit to make it more C++ like.
*/

/*  
  FastLZ - lightning-fast lossless compression library

  Copyright (C) 2007 Ariya Hidayat (ariya@kde.org)
  Copyright (C) 2006 Ariya Hidayat (ariya@kde.org)
  Copyright (C) 2005 Ariya Hidayat (ariya@kde.org)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include "M6Lib.h"

#include <cassert>

namespace
{

enum
{
	kMaxCopy	= 32,
	kMaxLen		= 264,
	kMaxDist	= 8192,
	
	kHashLog	= 13,
	kHashSize	= (1 << kHashLog),
	kHashMask	= (kHashSize - 1)	
};

inline uint16 mfastlz_hash(const uint8* p)
{
	uint16 p1 = *p++;
	uint16 p2 = *p++;
	uint16 p3 = *p++;

	uint16 v = p1 | (p2 << 8);
	v ^= (p2 | (p3 << 8)) ^ (v >> (16 - kHashLog));
	v &= kHashMask;
	
	return v;
}

}

size_t FastLZCompress(const void* input, size_t length,
	void* output, size_t maxout)
{
	const uint8* ip = static_cast<const uint8*>(input);
	const uint8* ip_bound = ip + length - 2;
	const uint8* ip_limit = ip + length - 12;
	uint8* op = static_cast<uint8*>(output);
	const uint8* op_limit = op + maxout;
	
	(void)op_limit;	// avoid compiler warnings in non debug mode
	
	// sanity check
	assert(maxout >= (105ULL * length) / 100);
	if (maxout < length + length / 20)
		return 0;
	
	if (length < 4)
	{
		if (length > 0 and maxout >= length + 1)
		{
			// create literal copy only
			*op++ = static_cast<uint8>(length - 1);
			
			assert(op < op_limit);
			
			++ip_bound;
			while (ip <= ip_bound)
				*op++ = *ip++;

			assert(op < op_limit);

			return static_cast<int32>(length + 1);
		}
		else
			return 0;
	}

	const uint8* htab[kHashSize];

	// initializes hash table
	for (const uint8** hslot = htab; hslot < htab + kHashSize; ++hslot)
		*hslot = ip;

	// we start with literal copy
	uint32 copy = 2;
	*op++ = kMaxCopy - 1;
	*op++ = *ip++;
	*op++ = *ip++;

	assert(op < op_limit);
	
	// main loop 
	while (ip < ip_limit)
	{
		// minimum match length 
		uint32 len = 3;
		
		// comparison starting-point 
		const uint8* anchor = ip;
		
		// find potential match 
		uint32 hval = mfastlz_hash(ip);
		
		assert(hval < kHashSize);
		
		const uint8** hslot = htab + hval;
		const uint8* ref = htab[hval];
		
		// calculate distance to the match 
		uint32 distance = static_cast<uint32>(anchor - ref);
		
		// update hash table 
		*hslot = anchor;
		
		// is this a match? check the first 3 bytes 
		if (distance == 0 or (distance >= kMaxDist) or
			*ref++ != *ip++ or *ref++ != *ip++ or *ref++ != *ip++)
		{
			*op++ = *anchor++;

			assert(op < op_limit);

			ip = anchor;
			++copy;
			if (copy == kMaxCopy)
			{
				copy = 0;
				*op++ = kMaxCopy - 1;

				assert(op < op_limit);
			}
			
			continue;
		}
		
		// last matched byte 
		ip = anchor + len;
		
		// distance is biased 
		--distance;
		
		if (distance == 0)
		{
			// zero distance means a run 
			uint8 x = ip[-1];
			
			while (ip < ip_bound and *ref++ == x)
				++ip;
		}
		else
		{
			while (ip < ip_bound and *ref++ == *ip++)
				;
		}
		
		// if we have copied something, adjust the copy count 
		if (copy > 0)
		{
			// copy is biased, '0' means 1 byte copy 
			*(op - copy - 1) = copy - 1;
			assert(op - copy - 1 >= output);
			assert(op - copy - 1 < op_limit);
		}
		else
			// back, to overwrite the copy count 
			--op;
		
		// reset literal counter 
		copy = 0;
		
		// length is biased, '1' means a match of 3 bytes 
		ip -= 3;
		len = static_cast<uint32>(ip - anchor);
		
		while (len > kMaxLen - 2)
		{
			*op++ = (7 << 5) + (distance >> 8);
			assert(op < op_limit);
			*op++ = kMaxLen - 2 - 7 - 2; 
			assert(op < op_limit);
			*op++ = (distance & 255);
			assert(op < op_limit);
			len -= kMaxLen - 2;
		}
		
		if (len < 7)
		{
			*op++ = (len << 5) + (distance >> 8);
			assert(op < op_limit);
			*op++ = (distance & 255);
			assert(op < op_limit);
		}
		else
		{
			*op++ = (7 << 5) + (distance >> 8);
			assert(op < op_limit);
			*op++ = len - 7;
			assert(op < op_limit);
			*op++ = (distance & 255);
			assert(op < op_limit);
		}
		
		// update the hash at match boundary 
		hval = mfastlz_hash(ip);
		assert(hval < kHashSize);
		htab[hval] = ip++;
		hval = mfastlz_hash(ip);
		assert(hval < kHashSize);
		htab[hval] = ip++;
		
		// assuming literal copy 
		*op++ = kMaxCopy - 1;
		assert(op < op_limit);
	}
	
	// left-over as literal copy 
	++ip_bound;
	while (ip <= ip_bound)
	{
		*op++ = *ip++;
		assert(op < op_limit);
		++copy;
		if (copy == kMaxCopy)
		{
			copy = 0;
			*op++ = kMaxCopy - 1;
			assert(op < op_limit);
		}
	}
	
	// if we have copied something, adjust the copy length 
	if (copy > 0)
	{
		assert(op - copy - 1 >= output);
		assert(op - copy - 1 < op_limit);
		*(op - copy - 1) = copy - 1;
	}
	else
		--op;
	
	assert(op > output);
	
	return static_cast<int32>(op - static_cast<uint8*>(output));
}

size_t FastLZDecompress(const void* input, size_t length,
	void* output, uint32 maxout)
{
	const uint8* ip = static_cast<const uint8*>(input);
	const uint8* ip_limit = ip + length;
	uint8* op = static_cast<uint8*>(output);
	uint8* op_limit = op + maxout;
	uint32 ctrl = (*ip++) & 31;
	bool loop = true;

	while (loop)
	{
		const uint8* ref = op;
		uint32 len = ctrl >> 5;
		uint32 ofs = (ctrl & 31) << 8;

		if (ctrl >= 32)
		{
			--len;
			ref -= ofs;

			if (len == 7 - 1)
				len += *ip++;

			ref -= *ip++;
			
			if (op + len + 3 > op_limit or ref - 1 < static_cast<uint8*>(output))
				return 0;

			if (ip < ip_limit)
				ctrl = *ip++;
			else
				loop = false;

			if (ref == op)
			{
				/* optimize copy for a run */
				uint8 b = ref[-1];
				
				*op++ = b;
				*op++ = b;
				*op++ = b;
				
				for (; len > 0; --len)
					*op++ = b;
			}
			else
			{
				/* copy from reference */
				ref--;

				*op++ = *ref++;
				*op++ = *ref++;
				*op++ = *ref++;
				
				for (; len > 0; --len)
					*op++ = *ref++;
			}
		}
		else
		{
			ctrl++;

			if (op + ctrl > op_limit or ip + ctrl > ip_limit)
				return 0;

			*op++ = *ip++; 

			for (--ctrl; ctrl; --ctrl)
				*op++ = *ip++;

			loop = ip < ip_limit;

			if (loop)
				ctrl = *ip++;
		}
	}

	return op - static_cast<uint8*>(output);
}

// --------------------------------------------------------------------

namespace detail
{

bool M6FastLZDecompressFilterImpl::filter(const char*& begin_in, const char* end_in,
				char*& begin_out, char* end_out, bool flush)
{
	
}

size_t FastLZDecompress(const void* input, size_t length,
	void* output, uint32 maxout)
{
	const uint8* ip = static_cast<const uint8*>(input);
	const uint8* ip_limit = ip + length;
	uint8* op = static_cast<uint8*>(output);
	uint8* op_limit = op + maxout;
	uint32 ctrl = (*ip++) & 31;
	bool loop = true;

	while (loop)
	{
		const uint8* ref = op;
		uint32 len = ctrl >> 5;
		uint32 ofs = (ctrl & 31) << 8;

		if (ctrl >= 32)
		{
			--len;
			ref -= ofs;

			if (len == 7 - 1)
				len += *ip++;

			ref -= *ip++;
			
			if (op + len + 3 > op_limit or ref - 1 < static_cast<uint8*>(output))
				return 0;

			if (ip < ip_limit)
				ctrl = *ip++;
			else
				loop = false;

			if (ref == op)
			{
				/* optimize copy for a run */
				uint8 b = ref[-1];
				
				*op++ = b;
				*op++ = b;
				*op++ = b;
				
				for (; len > 0; --len)
					*op++ = b;
			}
			else
			{
				/* copy from reference */
				ref--;

				*op++ = *ref++;
				*op++ = *ref++;
				*op++ = *ref++;
				
				for (; len > 0; --len)
					*op++ = *ref++;
			}
		}
		else
		{
			ctrl++;

			if (op + ctrl > op_limit or ip + ctrl > ip_limit)
				return 0;

			*op++ = *ip++; 

			for (--ctrl; ctrl; --ctrl)
				*op++ = *ip++;

			loop = ip < ip_limit;

			if (loop)
				ctrl = *ip++;
		}
	}

	return op - static_cast<uint8*>(output);
}


}
