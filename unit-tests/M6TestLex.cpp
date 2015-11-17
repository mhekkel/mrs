#include <iostream>
#include <ios>
#include <fstream>
#include <map>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <zeep/xml/document.hpp>
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

struct M6TokenTest
{
    const char*    text;
    M6Token        tokens[10];
    const char* words[10];
} kTestTokens[] = {
    { "aap", { eM6TokenWord, eM6TokenEOF }, { "aap" } },

    { "aap.", { eM6TokenWord, eM6TokenPunctuation, eM6TokenEOF }, { "aap", "." } },

    { "aap noot", { eM6TokenWord, eM6TokenWord, eM6TokenEOF }, { "aap", "noot" } },

    { "1 10 1e0 1.e0 1.0",
        { eM6TokenNumber, eM6TokenNumber, eM6TokenWord, eM6TokenNumber, eM6TokenPunctuation, eM6TokenWord, eM6TokenNumber, eM6TokenPunctuation, eM6TokenNumber, eM6TokenEOF },
        { "1", "10", "1e0", "1", ".", "e0", "1", ".", "0" }
    },

    {
        " 1e+0 1e-1",
        { eM6TokenWord, eM6TokenNumber, eM6TokenWord, eM6TokenNumber, eM6TokenEOF },
        { "1e", "0", "1e", "1" }
    },

//    { "10a 1e0a", { eM6TokenWord, eM6TokenWord, eM6TokenEOF } },
//    { "Q92834; B1ARN3; O00702;",
//        { eM6TokenWord, eM6TokenPunctuation, eM6TokenWord, eM6TokenPunctuation, eM6TokenWord, eM6TokenPunctuation, eM6TokenEOF } },
//    { "MHC I",
//        { eM6TokenWord, eM6TokenWord, eM6TokenEOF } },
};

BOOST_AUTO_TEST_CASE(test_tok_1)
{
    cout << "testing tokenizer 1" << endl;

    for (M6TokenTest& test : kTestTokens)
    {
        M6Tokenizer tok(test.text, strlen(test.text));
        uint32 i = 0;
        for (M6Token testToken : test.tokens)
        {
            M6Token token = tok.GetNextWord();
            BOOST_CHECK_EQUAL(token, testToken);
            if (test.words[i] != nullptr)
                BOOST_CHECK_EQUAL(tok.GetTokenString(), test.words[i]);
            if (token != testToken)
                cerr << "  " << test.words[i] << " != (" << tok.GetTokenString() << ')' << endl;
            if (token == eM6TokenEOF or testToken == eM6TokenEOF)
                break;
            ++i;
        }
    }
}

BOOST_AUTO_TEST_CASE(test_tok_2)
{
    cout << "testing tokenizer 2" << endl;

    M6Tokenizer tok("NMH I;", 5);
    BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenWord);
    BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenWord);
    BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenEOF);

    const char s2[] = "type 1\n";
    M6Tokenizer tok2(s2, strlen(s2));
    BOOST_CHECK_EQUAL(tok2.GetNextWord(), eM6TokenWord);
    BOOST_CHECK_EQUAL(tok2.GetNextWord(), eM6TokenNumber);
    BOOST_CHECK_EQUAL(tok2.GetNextWord(), eM6TokenEOF);
}

BOOST_AUTO_TEST_CASE(test_tok_3)
{
    cout << "testing tokenizer 3" << endl;

    M6Tokenizer tok("1", 1);
    BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenNumber);
    BOOST_CHECK_EQUAL(tok.GetTokenLength(), 1);
    BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenEOF);

    M6Tokenizer tok2("a", 1);
    BOOST_CHECK_EQUAL(tok2.GetNextWord(), eM6TokenWord);
    BOOST_CHECK_EQUAL(tok.GetTokenLength(), 1);
    BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenEOF);
}

BOOST_AUTO_TEST_CASE(test_tok_4)
{
    cout << "testing tokenizer 4 (normalization)" << endl;

    ifstream text("test/normalized-test.txt");
    BOOST_REQUIRE(text.is_open());

    int nr = 0;

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

        string::size_type p = line.find('\t');
        if (p == string::npos)
            continue;

        string w1 = line.substr(0, p);
        string w2 = line.substr(p + 1);

        M6Tokenizer::Normalize(w1);
        BOOST_CHECK_EQUAL(w1, w2);

//        M6Tokenizer tok(line.c_str(), line.length());
//        BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenWord);
//        string w1 = tok.GetTokenString();
//        BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenWord);
//        string w2 = tok.GetTokenString();
//        BOOST_CHECK_EQUAL(tok.GetNextWord(), eM6TokenEOF);
//        BOOST_CHECK_EQUAL(w1, w2);

        if (w1 != w2)
        {
            cout << nr << "\t" << line << "\t" << w1 << "\t!=\t" << w2 << endl;
            break;
        }
    }
}

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
            M6Token token = tokenizer.GetNextWord();
            if (token == eM6TokenEOF)
                break;

            if (token == eM6TokenNumber or token == eM6TokenWord)
            {
                string word(tokenizer.GetTokenValue(), tokenizer.GetTokenLength());
                assert(not word.empty());
                words.push_back(word);
                wordmap[lexicon.Store(word)] = word;
            }
        }
    }

    for (const string& word : words)
        BOOST_CHECK_EQUAL(wordmap[lexicon.Lookup(word)], word);

    for (uint32 t = 1; t < lexicon.Count(); ++t)
        BOOST_CHECK_EQUAL(lexicon.GetString(t), wordmap[t]);
}


BOOST_AUTO_TEST_CASE(test_lex_2)
{
    cout << "testing lexicon 2 (locale test)" << endl;

    ifstream text("test/test-duits.txt");
    BOOST_REQUIRE(text.is_open());

    string gruessen;

    for (int i = 0; i < 3; ++i)
    {
        string w;
        getline(text, w);

        M6Tokenizer tok(w.c_str(), w.length());
        M6Token token = tok.GetNextWord();
        BOOST_REQUIRE(token == eM6TokenWord);

        if (gruessen.empty())
            gruessen = tok.GetTokenString();
        else
            BOOST_CHECK_EQUAL(gruessen, tok.GetTokenString());
    }
}
