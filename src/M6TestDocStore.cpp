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
#include "M6Document.h"

#include <boost/test/unit_test.hpp>

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

BOOST_AUTO_TEST_CASE(test_store_1)
{
	cout << "testing document store (store)" << endl;

	ifstream text("test/pdbfind2-head.txt");
	BOOST_REQUIRE(text.is_open());

	if (fs::exists("test/pdbfind2.docs"))
		fs::remove("test/pdbfind2.docs");
	
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
			M6Document document;
			document.SetText(doc.str());
			store.StoreDocument(&document);
			++n;
			
			doc.str("");
			doc.clear();
		}
	}
	
	BOOST_CHECK_EQUAL(store.size(), n);
}

BOOST_AUTO_TEST_CASE(test_store_2)
{
	cout << "testing document store (retrieve-1)" << endl;

	ifstream text("test/pdbfind2-head.txt");
	BOOST_REQUIRE(text.is_open());

	M6DocStore store("test/pdbfind2.docs", eReadOnly);
	stringstream doc;
	uint32 n = 1;

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
			M6Document document;
			BOOST_CHECK(store.FetchDocument(n, document));

			string docA = document.GetText();
			string docB = doc.str();

			BOOST_CHECK_EQUAL(docA.length(), docB.length());
			BOOST_CHECK_EQUAL(docA, docB);

			++n;
			
			doc.str("");
			doc.clear();
		}
	}
	
	BOOST_CHECK_EQUAL(store.size(), n - 1);
}
