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

struct M6TokenTest
{
	const char*	text;
	M6Token		tokens[10];
} kTestTokens[] = {
	{ "aap", { eM6TokenWord, eM6TokenEOF } },
	{ "aap noot", { eM6TokenWord, eM6TokenWord, eM6TokenEOF } },
	{ "1 10 1e0 1.e0 1.0 1e+0 1e-1", { eM6TokenNumber, eM6TokenNumber, eM6TokenNumber, eM6TokenNumber, eM6TokenNumber, eM6TokenNumber, eM6TokenNumber, eM6TokenEOF } },
	{ "10a 1e0a", { eM6TokenWord, eM6TokenWord, eM6TokenEOF } },
	{ "Q92834; B1ARN3; O00702;",
		{ eM6TokenWord, eM6TokenPunctuation, eM6TokenWord, eM6TokenPunctuation, eM6TokenWord, eM6TokenPunctuation, eM6TokenEOF } },
	{ "MHC I",
		{ eM6TokenWord, eM6TokenWord, eM6TokenEOF } },	
};

BOOST_AUTO_TEST_CASE(test_tok_1)
{
	cout << "testing tokenizer 1" << endl;
	
	foreach (M6TokenTest& test, kTestTokens)
	{
		M6Tokenizer tok(test.text);
		foreach (M6Token testToken, test.tokens)
		{
			M6Token token = tok.GetToken();
			BOOST_CHECK_EQUAL(token, testToken);
			if (token != testToken)
				cerr << "  " << test.text << " != (" << string(tok.GetTokenValue(), tok.GetTokenLength()) << ')' << endl;
			if (token == eM6TokenEOF or testToken == eM6TokenEOF)
				break;
		}
	}
}

BOOST_AUTO_TEST_CASE(test_tok_2)
{
	cout << "testing tokenizer 2" << endl;
	
	M6Tokenizer tok("NMH I;", 5);
	BOOST_CHECK_EQUAL(tok.GetToken(), eM6TokenWord);
	BOOST_CHECK_EQUAL(tok.GetToken(), eM6TokenWord);
	BOOST_CHECK_EQUAL(tok.GetToken(), eM6TokenEOF);

	const char s2[] = "type 1\n";
	M6Tokenizer tok2(s2, strlen(s2));
	BOOST_CHECK_EQUAL(tok2.GetToken(), eM6TokenWord);
	BOOST_CHECK_EQUAL(tok2.GetToken(), eM6TokenNumber);
	BOOST_CHECK_EQUAL(tok2.GetToken(), eM6TokenEOF);
}
