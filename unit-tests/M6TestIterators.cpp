#include <iostream>


#include "M6Lib.h"
#include "M6Iterator.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_AUTO_TEST_CASE(test_intersection_iterator)
{
    cout << "testing intersection iterator" << endl;

    uint32 a[] = { 1, 4, 8 };
    uint32 b[] = { 1, 2, 5, 8, 9 };
    uint32 c[] = { 1, 8 };

    vector<uint32> va(a, a + sizeof(a) / sizeof(uint32));
    vector<uint32> vb(b, b + sizeof(b) / sizeof(uint32));
    vector<uint32> vc(c, c + sizeof(c) / sizeof(uint32));

    M6Iterator* ai = new M6VectorIterator(va);
    M6Iterator* bi = new M6VectorIterator(vb);

    M6Iterator* ui = new M6IntersectionIterator(ai, bi);

    vector<uint32> vt;
    uint32 doc; float rank;
    while (ui->Next(doc, rank))
        vt.push_back(doc);

    BOOST_CHECK(vt == vc);
}

BOOST_AUTO_TEST_CASE(test_union_iterator)
{
    cout << "testing union iterator" << endl;

    uint32 a[] = { 1, 4, 8 };
    uint32 b[] = { 1, 2, 5, 8, 9 };
    uint32 c[] = { 1, 2, 4, 5, 8, 9 };

    vector<uint32> va(a, a + sizeof(a) / sizeof(uint32));
    vector<uint32> vb(b, b + sizeof(b) / sizeof(uint32));
    vector<uint32> vc(c, c + sizeof(c) / sizeof(uint32));

    M6Iterator* ai = new M6VectorIterator(va);
    M6Iterator* bi = new M6VectorIterator(vb);

    M6Iterator* ui = new M6UnionIterator(ai, bi);

    vector<uint32> vt;
    uint32 doc; float rank;
    while (ui->Next(doc, rank))
        vt.push_back(doc);

    BOOST_CHECK(vt == vc);
}

