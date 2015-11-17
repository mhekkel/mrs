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

BOOST_AUTO_TEST_CASE(test_bit_stream_3)
{
    cout << "testing bitstream" << endl;

    M6OBitStream bits;

    uint32 v = 0x01234567;

    WriteBinary(bits, 32, v);
    M6IBitStream ibits(bits);

    uint32 t;
    ReadBinary(ibits, 32, t);
    BOOST_CHECK_EQUAL(t, v);
}

BOOST_AUTO_TEST_CASE(test_bit_stream_4)
{
    cout << "testing bitstream 4" << endl;

    M6OBitStream bits;

    uint32 a[] = { 3 };
    uint32 b[] = { 1, 2, 5, 8, 9 };
    uint32 c[] = { 1, 2, 4, 5, 8, 9 };

    vector<uint32> va(a, a + sizeof(a) / sizeof(uint32));
    vector<uint32> vb(b, b + sizeof(b) / sizeof(uint32));
    vector<uint32> vc(c, c + sizeof(c) / sizeof(uint32));

    WriteArray(bits, va);
    WriteArray(bits, vb);
    WriteArray(bits, vc);
    bits.Sync();

    M6OBitStream b2;
    CopyBits(b2, bits);
    b2.Sync();

    M6IBitStream b3(b2);
    vector<uint32> t;

    ReadArray(b3, t);    BOOST_CHECK(t == va);
    ReadArray(b3, t);    BOOST_CHECK(t == vb);
    ReadArray(b3, t);    BOOST_CHECK(t == vc);
}

BOOST_AUTO_TEST_CASE(test_bit_stream_5)
{
    cout << "testing bitstream 5" << endl;

    M6OBitStream bits;

    uint32 a[] = { 3 };
    uint32 b[] = { 1, 2, 5, 8, 9 };
    uint32 c[] = { 1, 2, 4, 5, 8, 9 };

    vector<uint32> va(a, a + sizeof(a) / sizeof(uint32));
    vector<uint32> vb(b, b + sizeof(b) / sizeof(uint32));
    vector<uint32> vc(c, c + sizeof(c) / sizeof(uint32));

    M6OBitStream ob1;    WriteArray(ob1, va);    CopyBits(bits, ob1);
    BOOST_CHECK_EQUAL(ob1.BitSize(), bits.BitSize());
    M6OBitStream ob2;    WriteArray(ob2, vb);    CopyBits(bits, ob2);
    BOOST_CHECK_EQUAL(ob1.BitSize() + ob2.BitSize(), bits.BitSize());
    M6OBitStream ob3;    WriteArray(ob3, vc);    CopyBits(bits, ob3);
    BOOST_CHECK_EQUAL(ob1.BitSize() + ob2.BitSize() + ob3.BitSize(), bits.BitSize());
    bits.Sync();

    M6IBitStream b3(bits);
    vector<uint32> t;

    ReadArray(b3, t);    BOOST_CHECK(t == va);
    ReadArray(b3, t);    BOOST_CHECK(t == vb);
    ReadArray(b3, t);    BOOST_CHECK(t == vc);
}

BOOST_AUTO_TEST_CASE(test_bit_stream_6)
{
    cout << "testing bitstream 6" << endl;

    M6OBitStream bits;

    for (int i = 0; i < 10; ++i)
    {
        vector<uint32> v(100 + i * 13);
        iota(v.begin(), v.end(), 100 + i * 13);

        M6OBitStream ob;
        WriteArray(ob, v);
        CopyBits(bits, ob);
    }

    bits.Sync();

    M6IBitStream ib(bits);

    for (int i = 0; i < 10; ++i)
    {
        vector<uint32> v(100 + i * 13);
        iota(v.begin(), v.end(), 100 + i * 13);

        vector<uint32> t;
        ReadArray(ib, t);
        BOOST_CHECK(t == v);
    }
}

BOOST_AUTO_TEST_CASE(test_bit_stream_7)
{
    cout << "testing bitstream 7" << endl;

    M6OBitStream bits;

    uint32 a[] = {
        3458, 3483, 3600, 5200, 5217, 5272, 5280, 5297,
        5343, 5386, 5475, 5490, 5536, 5572, 5596, 5661,
        5679, 5721, 5742, 6519, 6520, 6521, 6522
    };

    vector<uint32> v(a, a + sizeof(a) / sizeof(uint32));

    WriteArray(bits, v);

    M6OBitStream b2;
    WriteBits(b2, bits);

    {
        M6IBitStream ib(bits);
        vector<uint32> t;
        ReadArray(ib, t);

        BOOST_CHECK(t == v);
    }

    {
        M6IBitStream ib(b2);
        M6OBitStream ob;

        ReadBits(ib, ob);

        M6IBitStream ib2(ob);

        vector<uint32> t;
        ReadArray(ib2, t);

        BOOST_CHECK(t == v);
    }
}

BOOST_AUTO_TEST_CASE(test_bit_stream_8)
{
    cout << "testing bitstream 8" << endl;

    for (uint32 i = 1; i < 1000; ++i)
    {
        cerr << i << endl;

        M6OBitStream bits;

        for (uint32 j = 0; j < i; ++j)
            bits << 1;

        M6OBitStream b2;
        //CopyBits(b2, bits);
        swap(b2, bits);

        M6IBitStream ib1(b2);
        for (uint32 j = 0; j < i; ++j)
            BOOST_CHECK_EQUAL(ib1(), 1);
        BOOST_CHECK_EQUAL(ib1(), 0);

        M6OBitStream b3;
        WriteBits(b3, b2);

        M6IBitStream ib2(b3);
        M6OBitStream b4;
        ReadBits(ib2, b4);

        M6IBitStream ib3(b4);
        for (uint32 j = 0; j < i; ++j)
            BOOST_CHECK_EQUAL(ib3(), 1);
        BOOST_CHECK_EQUAL(ib3(), 0);
    }

}

BOOST_AUTO_TEST_CASE(test_bit_stream_9)
{
    cout << "testing bitstream 9" << endl;

    uint32 d[] = { 294, 771 };

    vector<uint32> docs(d, d + 2);

    M6OBitStream bits;
    WriteArray(bits, docs);

    M6IBitStream b2(bits);

    vector<uint32> d2;
    ReadArray(b2, d2);

    BOOST_CHECK(docs == d2);
}

