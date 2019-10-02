#include <iostream>
#include <vector>
#include <string>

#define BOOST_TEST_MODULE QueryTest
#include <boost/test/included/unit_test.hpp>

#include "M6Lib.h"
#include "M6Tokenizer.h"
#include "M6Iterator.h"
#include "M6Databank.h"

using namespace std;


int VERBOSE = 0;


BOOST_AUTO_TEST_CASE(TestParse1)
{
    std::string queryString = "resolution < 1.2";

    M6Tokenizer tokenizer(queryString);

    BOOST_CHECK_EQUAL(tokenizer.GetNextQueryToken(), eM6TokenWord);
    BOOST_CHECK_EQUAL(tokenizer.GetTokenString(), "resolution");

    BOOST_CHECK_EQUAL(tokenizer.GetNextQueryToken(), eM6TokenLessThan);

    BOOST_CHECK_EQUAL(tokenizer.GetNextQueryToken(), eM6TokenFloat);
    BOOST_CHECK_EQUAL(tokenizer.GetTokenString(), "1.2");
}

BOOST_AUTO_TEST_CASE(TestParse2)
{
    std::string queryString = "resolution<111";

    M6Tokenizer tokenizer(queryString);

    BOOST_CHECK_EQUAL(tokenizer.GetNextQueryToken(), eM6TokenWord);
    BOOST_CHECK_EQUAL(tokenizer.GetTokenString(), "resolution");

    BOOST_CHECK_EQUAL(tokenizer.GetNextQueryToken(), eM6TokenLessThan);

    BOOST_CHECK_EQUAL(tokenizer.GetNextQueryToken(), eM6TokenNumber);
    BOOST_CHECK_EQUAL(tokenizer.GetTokenString(), "111");
}

