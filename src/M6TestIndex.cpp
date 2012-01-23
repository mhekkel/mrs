#include <iostream>
#include <ios>
#include <fstream>
#include <map>

#include <boost/filesystem.hpp>
#include <zeep/xml/document.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>

#include "M6Lib.h"
#include "M6File.h"
#include "M6Index.h"
#include "M6Error.h"

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

//BOOST_AUTO_TEST_CASE(start_up)
//{
//	cout << "Hello, world!" << endl;
//}
//
//BOOST_AUTO_TEST_CASE(zeep_test)
//{
//	zeep::xml::document doc;
//
//	cout << "Hello, world!" << endl;
//}
//
//BOOST_AUTO_TEST_CASE(file_io)
//{
//	const char filename[] = "test-bestand.txt";
//
//	if (fs::exists(filename))
//		fs::remove(filename);
//
//	// read only a non existing file should fail
//	BOOST_CHECK_THROW(M6File(filename, eReadOnly), M6Exception);
//
//	// Create the file
//	M6File file(filename, eReadWrite);
//
//	// check if it exists
//	BOOST_CHECK(fs::exists("test-bestand.txt"));
//	
//	// Reading should fail
//	uint32 i = 0xcececece;
//	BOOST_CHECK_THROW(file.PRead(&i, sizeof(i), 0), M6Exception);
//	BOOST_CHECK_THROW(file.PRead(&i, sizeof(i), 1), M6Exception);
//
//	// Write an int at the start of the file
//	file.PWrite(&i, sizeof(i), 0);
//
//	// File should be one int long
//	BOOST_CHECK_EQUAL(file.Size(), sizeof(i));
//	BOOST_CHECK_EQUAL(fs::file_size(filename), sizeof(i));
//
//	// write another int 1 past the end
//	file.PWrite(&i, sizeof(i), sizeof(i) + 1);
//
//	// File should be two ints plus one long
//	BOOST_CHECK_EQUAL(file.Size(), 2 * sizeof(i) + 1);
//	BOOST_CHECK_EQUAL(fs::file_size(filename), 2 * sizeof(i) + 1);
//
//	file.Truncate(7);
//	BOOST_CHECK_EQUAL(file.Size(), 7);
//	BOOST_CHECK_EQUAL(fs::file_size(filename), 7);
//}

const char filename[] = "test.index";
const char* strings[] = {
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k",
	"l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v",
};

BOOST_AUTO_TEST_CASE(file_ix_1)
{
	if (fs::exists(filename))
		fs::remove(filename);

	int64 nr = 1;
	
	M6SimpleIndex indx(filename, true);
	
	foreach (const char* key, strings)
		indx.insert(key, nr++);

	nr = 1;
	foreach (const char* key, strings)
	{
		int64 v;
		BOOST_CHECK(indx.find(key, v));
		BOOST_CHECK_EQUAL(v, nr);
		++nr;
	}
}

//BOOST_AUTO_TEST_CASE(file_ix_2)
//{
//	M6SimpleIndex indx(filename, false);
//
//	int64 nr = 1;
//	foreach (const char* key, strings)
//	{
//		int64 v;
//		BOOST_CHECK(indx.find(key, v));
//		BOOST_CHECK_EQUAL(v, nr);
//		++nr;
//	}
//}

BOOST_AUTO_TEST_CASE(file_ix_3)
{
	if (fs::exists(filename))
		fs::remove(filename);

	M6SimpleIndex indx(filename, true);

	ifstream text("../../../test/test-doc-2.txt");
	BOOST_REQUIRE(text.is_open());

	map<string,int64> testix;

	int64 nr = 1;
	for (;;)
	{
		string word;
		text >> word;

		if (word.empty() and text.eof())
			break;

		ba::to_lower(word);
		
		if (testix.find(word) != testix.end())
			continue;

		//if (indx.find(word, v))
		//	continue;
		
		indx.insert(word, nr);
		testix[word] = nr;

//		int64 v;
//		BOOST_CHECK(indx.find(word, v));

		foreach (auto t, testix)
		{
			int64 v;
			BOOST_CHECK(indx.find(t.first, v));
			BOOST_CHECK_EQUAL(v, t.second);
		}

		++nr;
	}
	
	cout << "Created tree with " << indx.size()
		<< " keys and a depth of " << indx.depth() << endl;

	foreach (auto t, testix)
	{
		int64 v;
		BOOST_CHECK(indx.find(t.first, v));
		BOOST_CHECK_EQUAL(v, t.second);
	}
	
	nr = 0;
	//foreach (auto i, indx)
	for (auto i = indx.begin(); i != indx.end(); ++i)
	{
		BOOST_CHECK_EQUAL(testix[i->key], i->value);
		++nr;
	}
	
	BOOST_CHECK_EQUAL(nr, testix.size());

	indx.Vacuum();

	foreach (auto t, testix)
	{
		int64 v;
		BOOST_CHECK(indx.find(t.first, v));
		BOOST_CHECK_EQUAL(v, t.second);
	}
	
	nr = 0;
	//foreach (auto i, indx)
	for (auto i = indx.begin(); i != indx.end(); ++i)
	{
//		cout << i->key << " -> " << i->value << endl;

		BOOST_CHECK_EQUAL(testix[i->key], i->value);
		++nr;
	}
	
	BOOST_CHECK_EQUAL(nr, testix.size());
}

//BOOST_AUTO_TEST_CASE(file_ix_4)
//{
//	if (fs::exists(filename))
//		fs::remove(filename);
//
//	ifstream text("../../../test/test-doc-2.txt");
//	BOOST_REQUIRE(text.is_open());
//
//	map<string,int64> testix;
//
//	int64 nr = 1;
//	for (;;)
//	{
//		string word;
//		text >> word;
//
//		if (word.empty() and text.eof())
//			break;
//
//		ba::to_lower(word);
//		
//		testix[word] = nr++;
//	}
//	
//	map<string,int64>::iterator i = testix.begin();
//
//	M6SortedInputIterator data = 
//		[&testix, &i](M6Tuple& outTuple) -> bool
//		{
//			bool result = false;
//			if (i != testix.end())
//			{
//				outTuple.key = i->first;
//				outTuple.value = i->second;
//				++i;
//				result = true;
//			}
//			return result;
//		};
//	
//	M6SimpleIndex indx(filename, data);
//
//	foreach (auto t, testix)
//	{
//		int64 v;
//		BOOST_CHECK(indx.find(t.first, v));
//		BOOST_CHECK_EQUAL(v, t.second);
//	}
//	
//	nr = 0;
//	//foreach (auto i, indx)
//	for (auto i = indx.begin(); i != indx.end(); ++i)
//	{
////		cout << i->key << " -> " << i->value << endl;
//
//		BOOST_CHECK_EQUAL(testix[i->key], i->value);
//		++nr;
//	}
//	
//	BOOST_CHECK_EQUAL(nr, testix.size());
//
//	indx.Vacuum();
//
//	foreach (auto t, testix)
//	{
//		int64 v;
//		BOOST_CHECK(indx.find(t.first, v));
//		BOOST_CHECK_EQUAL(v, t.second);
//	}
//	
//	nr = 0;
//	//foreach (auto i, indx)
//	for (auto i = indx.begin(); i != indx.end(); ++i)
//	{
////		cout << i->key << " -> " << i->value << endl;
//
//		BOOST_CHECK_EQUAL(testix[i->key], i->value);
//		++nr;
//	}
//
//	BOOST_CHECK_EQUAL(nr, testix.size());
//}	
