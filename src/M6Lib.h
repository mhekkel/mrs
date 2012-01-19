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
