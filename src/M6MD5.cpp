//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <cstring>

#include "M6MD5.h"

using namespace std;

M6MD5::M6MD5()
	: mDataLength(0)
	, mBitLength(0)
{
	mBuffer[0] = 0x67452301;
	mBuffer[1] = 0xefcdab89;
	mBuffer[2] = 0x98badcfe;
	mBuffer[3] = 0x10325476;
}

M6MD5::M6MD5(const void* inData, size_t inLength)
	: mDataLength(0)
	, mBitLength(0)
{
	mBuffer[0] = 0x67452301;
	mBuffer[1] = 0xefcdab89;
	mBuffer[2] = 0x98badcfe;
	mBuffer[3] = 0x10325476;
	
	Update(inData, inLength);
}

M6MD5::M6MD5(const string& inData)
	: mDataLength(0)
	, mBitLength(0)
{
	mBuffer[0] = 0x67452301;
	mBuffer[1] = 0xefcdab89;
	mBuffer[2] = 0x98badcfe;
	mBuffer[3] = 0x10325476;
	
	Update(inData.c_str(), inData.length());
}

void M6MD5::Update(const void* inData, size_t inLength)
{
	mBitLength += inLength * 8;
	
	const uint8* p = reinterpret_cast<const uint8*>(inData);
	
	if (mDataLength > 0)
	{
		uint32 n = 64 - mDataLength;
		if (n > inLength)
			n = static_cast<uint32>(inLength);
		
		memcpy(mData + mDataLength, p, n);
		
		if (mDataLength == 64)
		{
			Transform(mData);
			mDataLength = 0;
		}
		
		p += n;
		inLength -= n;
	}
	
	while (inLength >= 64)
	{
		Transform(p);
		p += 64;
		inLength -= 64;
	}
	
	if (inLength > 0)
	{
		memcpy(mData, p, inLength);
		mDataLength += static_cast<uint32>(inLength);
	}
}

string M6MD5::Finalise()
{
	mData[mDataLength] = 0x80;
	++mDataLength;
	fill(mData + mDataLength, mData + 64, 0);
	
	if (64 - mDataLength < 8)
	{
		Transform(mData);
		fill(mData, mData + 56, 0);
	}
	
	uint8* p = mData + 56;
	for (int i = 0; i < 8; ++i)
		*p++ = static_cast<uint8>((mBitLength >> (i * 8)));
	
	Transform(mData);
	fill(mData, mData + 64, 0);
	
	// convert to hex
	const char kHexChars[] = "0123456789abcdef";
	string result;
	result.reserve(32);
	
	for (int i = 0; i < 4; ++ i)
	{
		for (int j = 0; j < 4; ++j)
		{
			uint8 b = mBuffer[i] >> (j * 8);
			result += kHexChars[b >> 4];
			result += kHexChars[b & 0x0f];
		}
	}
	
	return result;
}


#define F1(x,y,z)	(z ^ (x & (y ^ z)))
#define F2(x,y,z)	F1(z, x, y)
#define F3(x,y,z)	(x ^ y ^ z)
#define	F4(x,y,z)	(y ^ ( x | ~z))

#define STEP(F,w,x,y,z,data,s) 	\
	w += F(x, y, z) + data;		\
	w = w << s | w >> (32 - s);	\
	w += x;

void M6MD5::Transform(const uint8* inData)
{
	uint32 a = mBuffer[0], b = mBuffer[1], c = mBuffer[2], d = mBuffer[3];
	uint32 in[16];
	
	for (int i = 0; i < 16; ++i)
	{
		in[i] = static_cast<uint32>(inData[0]) <<  0 |
				static_cast<uint32>(inData[1]) <<  8 |
				static_cast<uint32>(inData[2]) << 16 |
				static_cast<uint32>(inData[3]) << 24;
		inData += 4;
	}
	
	STEP(F1, a, b, c, d, in[ 0] + 0xd76aa478,  7);
	STEP(F1, d, a, b, c, in[ 1] + 0xe8c7b756, 12);
	STEP(F1, c, d, a, b, in[ 2] + 0x242070db, 17);
	STEP(F1, b, c, d, a, in[ 3] + 0xc1bdceee, 22);
	STEP(F1, a, b, c, d, in[ 4] + 0xf57c0faf,  7);
	STEP(F1, d, a, b, c, in[ 5] + 0x4787c62a, 12);
	STEP(F1, c, d, a, b, in[ 6] + 0xa8304613, 17);
	STEP(F1, b, c, d, a, in[ 7] + 0xfd469501, 22);
	STEP(F1, a, b, c, d, in[ 8] + 0x698098d8,  7);
	STEP(F1, d, a, b, c, in[ 9] + 0x8b44f7af, 12);
	STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
	STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
	STEP(F1, a, b, c, d, in[12] + 0x6b901122,  7);
	STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
	STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
	STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

	STEP(F2, a, b, c, d, in[ 1] + 0xf61e2562,  5);
	STEP(F2, d, a, b, c, in[ 6] + 0xc040b340,  9);
	STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
	STEP(F2, b, c, d, a, in[ 0] + 0xe9b6c7aa, 20);
	STEP(F2, a, b, c, d, in[ 5] + 0xd62f105d,  5);
	STEP(F2, d, a, b, c, in[10] + 0x02441453,  9);
	STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
	STEP(F2, b, c, d, a, in[ 4] + 0xe7d3fbc8, 20);
	STEP(F2, a, b, c, d, in[ 9] + 0x21e1cde6,  5);
	STEP(F2, d, a, b, c, in[14] + 0xc33707d6,  9);
	STEP(F2, c, d, a, b, in[ 3] + 0xf4d50d87, 14);
	STEP(F2, b, c, d, a, in[ 8] + 0x455a14ed, 20);
	STEP(F2, a, b, c, d, in[13] + 0xa9e3e905,  5);
	STEP(F2, d, a, b, c, in[ 2] + 0xfcefa3f8,  9);
	STEP(F2, c, d, a, b, in[ 7] + 0x676f02d9, 14);
	STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

	STEP(F3, a, b, c, d, in[ 5] + 0xfffa3942,  4);
	STEP(F3, d, a, b, c, in[ 8] + 0x8771f681, 11);
	STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
	STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
	STEP(F3, a, b, c, d, in[ 1] + 0xa4beea44,  4);
	STEP(F3, d, a, b, c, in[ 4] + 0x4bdecfa9, 11);
	STEP(F3, c, d, a, b, in[ 7] + 0xf6bb4b60, 16);
	STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
	STEP(F3, a, b, c, d, in[13] + 0x289b7ec6,  4);
	STEP(F3, d, a, b, c, in[ 0] + 0xeaa127fa, 11);
	STEP(F3, c, d, a, b, in[ 3] + 0xd4ef3085, 16);
	STEP(F3, b, c, d, a, in[ 6] + 0x04881d05, 23);
	STEP(F3, a, b, c, d, in[ 9] + 0xd9d4d039,  4);
	STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
	STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
	STEP(F3, b, c, d, a, in[ 2] + 0xc4ac5665, 23);

	STEP(F4, a, b, c, d, in[ 0] + 0xf4292244,  6);
	STEP(F4, d, a, b, c, in[ 7] + 0x432aff97, 10);
	STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
	STEP(F4, b, c, d, a, in[ 5] + 0xfc93a039, 21);
	STEP(F4, a, b, c, d, in[12] + 0x655b59c3,  6);
	STEP(F4, d, a, b, c, in[ 3] + 0x8f0ccc92, 10);
	STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
	STEP(F4, b, c, d, a, in[ 1] + 0x85845dd1, 21);
	STEP(F4, a, b, c, d, in[ 8] + 0x6fa87e4f,  6);
	STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
	STEP(F4, c, d, a, b, in[ 6] + 0xa3014314, 15);
	STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
	STEP(F4, a, b, c, d, in[ 4] + 0xf7537e82,  6);
	STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
	STEP(F4, c, d, a, b, in[ 2] + 0x2ad7d2bb, 15);
	STEP(F4, b, c, d, a, in[ 9] + 0xeb86d391, 21);
	
	mBuffer[0] += a;
	mBuffer[1] += b;
	mBuffer[2] += c;
	mBuffer[3] += d;
}
