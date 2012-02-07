﻿#include "M6Lib.h"

#include <boost/test/unit_test.hpp>

#include "M6BitStream.h"

using namespace std;

BOOST_AUTO_TEST_CASE(test_bit_stream_1)
{
	cout << "testing bitstream" << endl;

	M6OBitStream bits;
	
	for (uint32 i = 1; i < 100; ++i)
		WriteGamma(bits, i);
	bits.Sync();
	
	M6IBitStream ibits(bits);
	
	for (uint32 i = 1; i < 100; ++i)
	{
		uint32 v;
		ReadGamma(ibits, v);
		BOOST_CHECK_EQUAL(i, v);
	}
}
