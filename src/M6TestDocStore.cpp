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
#include "M6Lexicon.h"
#include "M6DocStore.h"

#include <boost/test/unit_test.hpp>

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

BOOST_AUTO_TEST_CASE(test_store_1)
{
	cout << "testing document store" << endl;

	ifstream text("test/pdbfind2-head.txt");
	BOOST_REQUIRE(text.is_open());
	
	M6DocStore store("test/pdbfind2.docs", eReadWrite);
	stringstream doc;
	uint32 n = 0;

	for (;;)
	{
		string line;
		getline(text, line);

		if (line.empty())
		{
			if (text.eof())
				break;
			continue;
		}
		
		doc << line << endl;
		
		if (line == "//")
		{
			M6Document document(doc.str());
			store.StoreDocument(document);
			++n;
			
			doc.str("");
			doc.clear();
		}
	}
	
	BOOST_CHECK_EQUAL(store.size(), n);
}
