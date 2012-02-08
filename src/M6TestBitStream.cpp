#include "M6Lib.h"

#include <iostream>
#include <numeric>
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

BOOST_AUTO_TEST_CASE(test_bit_stream_2)
{
	cout << "testing bitstream" << endl;

	vector<uint32> a(1000);
	iota(a.begin(), a.end(), 1);
	for_each(a.begin(), a.end(), [](uint32& i) { i *= 20; });

	M6OBitStream bits;
	
	WriteArray(bits, a, 1000);
	
//	bits.Sync();
//	
	M6IBitStream ibits(bits);
	//
	//foreach (uint32 i, a)
	//{
	//	uint32 v;
	//	ReadGamma(ibits, v);
	//	BOOST_CHECK_EQUAL(i, v);
	//}

	M6OBitStream b2;
	CopyBits(b2, bits);
}
