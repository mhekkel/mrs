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

#endif

#include <boost/cstdint.hpp>

typedef boost::int8_t		int8;
typedef boost::uint8_t		uint8;
typedef boost::int16_t		int16;
typedef boost::uint16_t		uint16;
typedef boost::int32_t		int32;
typedef boost::uint32_t		uint32;
typedef boost::int64_t		int64;
typedef boost::uint64_t		uint64;

// Some byte swapping code
// helper class, effectively a noop

struct no_swapper
{
	template<typename T>
	static T	swap(T inValue)		{ return inValue; }
};

struct byte_swapper
{
	template<typename T>
	static T	swap(T inValue)
	{
		this_will_not_compile_I_hope(inValue);
	}
};

template<>
inline
int64 byte_swapper::swap(int64 inValue)
{
	return static_cast<int64>(
		(((static_cast<uint64>(inValue))<<56) & 0xFF00000000000000ULL)  |
		(((static_cast<uint64>(inValue))<<40) & 0x00FF000000000000ULL)  |
		(((static_cast<uint64>(inValue))<<24) & 0x0000FF0000000000ULL)  |
		(((static_cast<uint64>(inValue))<< 8) & 0x000000FF00000000ULL)  |
		(((static_cast<uint64>(inValue))>> 8) & 0x00000000FF000000ULL)  |
		(((static_cast<uint64>(inValue))>>24) & 0x0000000000FF0000ULL)  |
		(((static_cast<uint64>(inValue))>>40) & 0x000000000000FF00ULL)  |
		(((static_cast<uint64>(inValue))>>56) & 0x00000000000000FFULL));
}

template<>
inline
uint64 byte_swapper::swap(uint64 inValue)
{
	return static_cast<uint64>(
		((((uint64)inValue)<<56) & 0xFF00000000000000ULL)  |
		((((uint64)inValue)<<40) & 0x00FF000000000000ULL)  |
		((((uint64)inValue)<<24) & 0x0000FF0000000000ULL)  |
		((((uint64)inValue)<< 8) & 0x000000FF00000000ULL)  |
		((((uint64)inValue)>> 8) & 0x00000000FF000000ULL)  |
		((((uint64)inValue)>>24) & 0x0000000000FF0000ULL)  |
		((((uint64)inValue)>>40) & 0x000000000000FF00ULL)  |
		((((uint64)inValue)>>56) & 0x00000000000000FFULL));
}

template<>
inline
int32 byte_swapper::swap(int32 inValue)
{
	return static_cast<int32>(
			((((uint32)inValue)<<24) & 0xFF000000)  |
			((((uint32)inValue)<< 8) & 0x00FF0000)  |
			((((uint32)inValue)>> 8) & 0x0000FF00)  |
			((((uint32)inValue)>>24) & 0x000000FF));
}

template<>
inline
uint32 byte_swapper::swap(uint32 inValue)
{
	return static_cast<uint32>(
			((((uint32)inValue)<<24) & 0xFF000000)  |
			((((uint32)inValue)<< 8) & 0x00FF0000)  |
			((((uint32)inValue)>> 8) & 0x0000FF00)  |
			((((uint32)inValue)>>24) & 0x000000FF));
}

template<>
inline
float byte_swapper::swap(float inValue)
{
	union {
		float	a;
		uint32	b;
	} v;
	
	v.a = inValue;
	
	v.b = static_cast<uint32>(
			((v.b<<24) & 0xFF000000)  |
			((v.b<< 8) & 0x00FF0000)  |
			((v.b>> 8) & 0x0000FF00)  |
			((v.b>>24) & 0x000000FF));
	
	return v.a;
}

template<>
inline
int16 byte_swapper::swap(int16 inValue)
{
	return static_cast<int16>(
			((((uint16)inValue)<< 8) & 0xFF00)  |
			((((uint16)inValue)>> 8) & 0x00FF));
}

template<>
inline
uint16 byte_swapper::swap(uint16 inValue)
{
	return static_cast<uint16>(
			((((uint16)inValue)<< 8) & 0xFF00)  |
			((((uint16)inValue)>> 8) & 0x00FF));
}

template<>
inline
int8 byte_swapper::swap(int8 inValue)
{
	return inValue;
}

template<>
inline
uint8 byte_swapper::swap(uint8 inValue)
{
	return inValue;
}

template<>
inline
bool byte_swapper::swap(bool inValue)
{
	return inValue;
}

#if BIGENDIAN
typedef no_swapper		net_swapper;
#else
typedef byte_swapper	net_swapper;
#endif

