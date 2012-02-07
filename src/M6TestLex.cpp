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

#include <boost/test/unit_test.hpp>

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

BOOST_AUTO_TEST_CASE(test_lex_1)
{
	cout << "testing lexicon" << endl;

	ifstream text("test/test-doc.txt");
	BOOST_REQUIRE(text.is_open());

	M6Lexicon lexicon;
	map<uint32,string> wordmap;
	vector<string> words;

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

		M6Tokenizer tokenizer(line.c_str(), line.length());

		for (;;)
		{
			M6Token token = tokenizer.GetToken();
			if (token == eM6TokenEOF)
				break;
			
			if (token == eM6TokenNumber or token == eM6TokenWord)
			{
				string word(tokenizer.GetTokenValue(), tokenizer.GetTokenLength());
				words.push_back(word);
				wordmap[lexicon.Store(word)] = word;
			}
		}
	}

	foreach (const string& word, words)
		BOOST_CHECK_EQUAL(wordmap[lexicon.Lookup(word)], word);

	for (uint32 t = 1; t < lexicon.Count(); ++t)
		BOOST_CHECK_EQUAL(lexicon.GetString(t), wordmap[t]);
}

BOOST_AUTO_TEST_CASE(test_tok_1)
{
	cout << "testing tokenizer 1" << endl;
	
	M6Tokenizer tok("aap noot mies");
	M6Token tokens[] = { eM6TokenWord, eM6TokenOther, eM6TokenWord, eM6TokenOther, eM6TokenWord, eM6TokenEOF };
	
	foreach (M6Token token, tokens)
		BOOST_CHECK_EQUAL(token, tok.GetToken());
}

BOOST_AUTO_TEST_CASE(test_tok_2)
{
	M6Tokenizer tok("1 10 100");
	M6Token tokens[] = { eM6TokenNumber, eM6TokenOther, eM6TokenNumber, eM6TokenOther, eM6TokenNumber, eM6TokenEOF };
	
	foreach (M6Token token, tokens)
		BOOST_CHECK_EQUAL(token, tok.GetToken());
}

BOOST_AUTO_TEST_CASE(test_tok_3)
{
	M6Tokenizer tok("1. 1.0 1e+0");
	M6Token tokens[] = { eM6TokenNumber, eM6TokenOther, eM6TokenNumber, eM6TokenOther, eM6TokenNumber, eM6TokenEOF };
	
	foreach (M6Token token, tokens)
		BOOST_CHECK_EQUAL(token, tok.GetToken());
}

BOOST_AUTO_TEST_CASE(test_tok_4)
{
	M6Tokenizer tok("1e0a 10a 00_1");
	M6Token tokens[] = { eM6TokenWord, eM6TokenOther, eM6TokenWord, eM6TokenOther, eM6TokenWord, eM6TokenEOF };
	
	foreach (M6Token token, tokens)
		BOOST_CHECK_EQUAL(token, tok.GetToken());
}

