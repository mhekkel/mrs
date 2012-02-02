#include <iostream>
#include <ios>
#include <fstream>
#include <map>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <zeep/xml/document.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "M6Lib.h"
#include "M6File.h"
#include "M6Index.h"
#include "M6Tokenizer.h"
#include "M6Error.h"

#define BOOST_TEST_MAIN
//#define BOOST_TEST_MODULE MyTest
//#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
//#include <boost/test/minimal.hpp>
//#include <boost/test/included/unit_test.hpp>

