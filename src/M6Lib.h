#pragma once

#if defined(_MSC_VER)

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include <SDKDDKVer.h>
#include <iso646.h>

#undef CreateFile

#undef fopen
#undef fclose
#undef fread

#pragma warning (disable : 4355)	// this is used in Base Initializer list
#pragma warning (disable : 4996)	// unsafe function or variable
#pragma warning (disable : 4068)	// unknown pragma
#pragma warning (disable : 4996)	// stl copy()
#pragma warning (disable : 4800)	// BOOL conversion

#pragma comment ( lib, "libzeep" )
#pragma comment ( lib, "libm6" )
#pragma comment ( lib, "libpcre" )
//#pragma comment ( lib, "libz" )

#if defined(_DEBUG)
#	define DEBUG	1
#endif

#endif

#include <boost/cstdint.hpp>
#include <boost/type_traits/is_integral.hpp>

typedef boost::int8_t		int8;
typedef boost::uint8_t		uint8;
typedef boost::int16_t		int16;
typedef boost::uint16_t		uint16;
typedef boost::int32_t		int32;
typedef boost::uint32_t		uint32;
typedef boost::int64_t		int64;
typedef boost::uint64_t		uint64;

//enum
//{
//	kM6BigEndianOrder = 0x0102,
//	kM6LittleEndianOrder = 0x0201
//};
//
//static const union {
//	uint8	b[2];
//	uint16	v;
//} kM6ByteOrder = { { 1, 2 } };
//
//#define M6_BIG_ENDIAN		(kM6ByteOrder.v == kM6BigEndianOrder)
//#define M6_LITTLE_ENDIAN	(kM6ByteOrder.v == kM6LittleEndianOrder)
//
//struct
//
//
//#if M6_BIG_ENDIAN
//#define FOUR_CHAR_INLINE(x)	x
//#else
//#define FOUR_CHAR_INLINE(x)     \
//			((((x)<<24) & 0xFF000000UL)|\
//			 (((x)<<8 ) & 0x00FF0000UL)|\
//			 (((x)>>8 ) & 0x0000FF00UL)|\
//			 (((x)>>24) & 0x000000FFUL))
//#endif

// --------------------------------------------------------------------
// some types used throughout m6

enum M6DataType
{
	eM6NoData,
	
	eM6TextData			= 1,
	eM6StringData,
	eM6NumberData,
	eM6DateData
};

enum M6IndexType : uint32
{
	eM6CharIndex			= 'M6cu',
	eM6NumberIndex			= 'M6nu',
//	eM6DateIndex			= 'M6du',
	eM6CharMultiIndex		= 'M6cm',
	eM6NumberMultiIndex		= 'M6nm',
//	eM6DateMultiIndex		= 'M6dm',
	eM6CharMultiIDLIndex	= 'M6ci',
	eM6CharWeightedIndex	= 'M6cw'
};

extern const uint32
	kM6MaxWeight, kM6WeightBitCount;

extern int VERBOSE;

// --------------------------------------------------------------------


// Some byte swapping code

template<class T, int S = sizeof(T), bool I = boost::is_integral<T>::value>
struct swapper
{
};

template<class T>
struct swapper<T, 1, true>
{
	T operator()(T v) const
	{
		return v;
	}
};

template<class T>
struct swapper<T, 2, true>
{
	T operator()(T v) const
	{
		return static_cast<T>(
			((static_cast<uint16>(v)<< 8) & 0xFF00)  |
			((static_cast<uint16>(v)>> 8) & 0x00FF));
	}
};

template<class T>
struct swapper<T, 4, true>
{
	T operator()(T v) const
	{
		return static_cast<T>(
			((static_cast<uint32>(v)<<24) & 0xFF000000)  |
			((static_cast<uint32>(v)<< 8) & 0x00FF0000)  |
			((static_cast<uint32>(v)>> 8) & 0x0000FF00)  |
			((static_cast<uint32>(v)>>24) & 0x000000FF));
	}
};

template<class T>
struct swapper<T, 8, true>
{
	T operator()(T v) const
	{
		return static_cast<T>(
			((static_cast<uint64>(v)<<56) & 0xFF00000000000000ULL)  |
			((static_cast<uint64>(v)<<40) & 0x00FF000000000000ULL)  |
			((static_cast<uint64>(v)<<24) & 0x0000FF0000000000ULL)  |
			((static_cast<uint64>(v)<< 8) & 0x000000FF00000000ULL)  |
			((static_cast<uint64>(v)>> 8) & 0x00000000FF000000ULL)  |
			((static_cast<uint64>(v)>>24) & 0x0000000000FF0000ULL)  |
			((static_cast<uint64>(v)>>40) & 0x000000000000FF00ULL)  |
			((static_cast<uint64>(v)>>56) & 0x00000000000000FFULL));
	}
};

template<>
struct swapper<float>
{
	float operator()(float v) const
	{
		union
		{
			uint32	vi;
			float	vf;
		} u;

		u.vf = v;
		u.vi = swapper<uint32>().operator()(u.vi);

		return u.vf;
	}
};


#if BIGENDIAN
template<class T>
inline
T swap_bytes(T v)
{
	return v;
}
#else
template<class T>
inline
T swap_bytes(T v)
{
	swapper<T> swap;
	return swap(v);
}
#endif

