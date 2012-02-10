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
	
	CompressSimpleArraySelector(bits, a);
	
	cout << "bitsize: " << bits.Size() << endl;

	M6IBitStream ibits(bits);
	M6CompressedArray arr(ibits, 1000);
	
	auto ai = a.begin();
	auto bi = arr.begin();

	for (int i = 0; i < 1000; ++i)
	{
		BOOST_CHECK_EQUAL(*ai, *bi);
		++ai;
		++bi;
	}

	BOOST_CHECK(ai == a.end());
	BOOST_CHECK(bi == arr.end());

	M6OBitStream b2;
	CopyBits(b2, bits);

	M6IBitStream ibits2(b2);
	M6CompressedArray arr2(ibits2, 1000);

	auto a2i = a.begin();
	auto b2i = arr2.begin();

	for (int i = 0; i < 1000; ++i)
	{
		BOOST_CHECK_EQUAL(*a2i, *b2i);
		++a2i;
		++b2i;
	}

	BOOST_CHECK(a2i == a.end());
	BOOST_CHECK(b2i == arr2.end());
}
