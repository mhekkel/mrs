#pragma once

//#if defined(BIGENDIAN)
//#define FOUR_CHAR_INLINE(x)	x
//#else
//#define FOUR_CHAR_INLINE(x)     \
//			((((x)<<24) & 0xFF000000UL)|\
//			 (((x)<<8 ) & 0x00FF0000UL)|\
//			 (((x)>>8 ) & 0x0000FF00UL)|\
//			 (((x)>>24) & 0x000000FFUL))
//#endif

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
//#pragma comment ( lib, "libbz2" )
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

#if 1 //BIGENDIAN
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

