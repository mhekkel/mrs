//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

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
#if not defined(BUILDING_M6_EXE)
#pragma comment ( lib, "libm6" )
#endif

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

// --------------------------------------------------------------------
// some types used throughout m6

enum M6DataType
{
	eM6NoData,
	
	eM6TextData			= 1,
	eM6StringData,
	eM6NumberData,
	eM6DateData,
    eM6FloatData,
};

enum M6IndexType : uint32
{
	eM6CharIndex			= 'M6cu',
	eM6NumberIndex			= 'M6nu',
    eM6FloatIndex           = 'M6db',
//	eM6DateIndex			= 'M6du',
	eM6CharMultiIndex		= 'M6cm',
	eM6NumberMultiIndex		= 'M6nm',
    eM6FloatMultiIndex      = 'M6dm',
//	eM6DateMultiIndex		= 'M6dm',
	eM6CharMultiIDLIndex	= 'M6ci',
	eM6CharWeightedIndex	= 'M6cw',
	
	// special name
	eM6LinkIndex			= 'M6ln'
};

enum M6QueryOperator
{
	eM6Contains,
	eM6LessThan,
	eM6LessOrEqual,
	eM6Equals,
	eM6GreaterOrEqual,
	eM6GreaterThan
};

enum : uint32
{
    kM6WeightBitCount = 5,
    kM6MaxWeight = (1 << kM6WeightBitCount) - 1,
};

extern int VERBOSE;
