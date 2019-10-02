#include <iostream>
#include <vector>
#include <string>

#define BOOST_TEST_MODULE QueryTest
#include <boost/test/included/unit_test.hpp>

#include "M6Lib.h"
#include "M6Query.h"
#include "M6Iterator.h"
#include "M6Databank.h"

using namespace std;


int VERBOSE = 0;

BOOST_AUTO_TEST_CASE(TestQuery1)
{
    vector<pair<string,string>> indexNames;
    unique_ptr<M6Databank> databank(M6Databank::CreateNew("test-db", "test/test-db.m6", "0.0.0", indexNames));

    M6Iterator* filter = nullptr;
    bool isBooleanQuery;
    vector<string> terms;

    ParseQuery(*(databank.get()), "hyhel-5", true, terms, filter, isBooleanQuery);

    BOOST_CHECK_EQUAL(1, terms.size());
    BOOST_CHECK_EQUAL(isBooleanQuery, false);

    ParseQuery(*(databank.get()), "hyhel -5", true, terms, filter, isBooleanQuery);
    BOOST_CHECK_EQUAL(2, terms.size());
}


BOOST_AUTO_TEST_CASE(TestQuery2)
{
    vector<pair<string,string>> indexNames;
    unique_ptr<M6Databank> databank(M6Databank::CreateNew("test-db", "test/test-db.m6", "0.0.0", indexNames));

    M6Iterator* filter = nullptr;
    bool isBooleanQuery;
    vector<string> terms;

    ParseQuery(*(databank.get()), "resolution < 1.2", true, terms, filter, isBooleanQuery);

    BOOST_CHECK_EQUAL(0, terms.size());
    BOOST_CHECK_EQUAL(isBooleanQuery, true);
}
