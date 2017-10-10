//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include "M6Query.h"
#include "M6Tokenizer.h"
#include "M6Iterator.h"
#include "M6Error.h"
#include "M6Databank.h"

using namespace std;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

class M6QueryParser
{
  public:
                    M6QueryParser(M6Databank* inDatabank, const string& inQuery,
                        bool inAllTermsRequired);

    void            Parse(vector<string>& outTerms, M6Iterator*& outFilter);
    bool            IsBooleanQuery() const    { return mIsBooleanQuery; }

  private:
    M6Iterator*        ParseQuery();
    M6Iterator*        ParseTest();
    M6Iterator*        ParseLink();
    M6Iterator*        ParseQualifiedTest(const string& inIndex);
    M6Iterator*        ParseTerm(const string& inIndex);
    M6Iterator*        ParseBooleanTerm(const string& inIndex, M6QueryOperator inOperator);
    M6Iterator*        ParseBetween(const string& inIndex);
    M6Iterator*        ParseString();

    M6Token            GetNextToken();
    void            Match(M6Token inToken);

    M6Iterator*        GetLinks(const string& inDB, const string& inDocID);
    M6Iterator*        GetLinks(const string& inDB, uint32 inDocNr);

    M6Databank*        mDatabank;
    M6Tokenizer        mTokenizer;
    bool            mImplicitIntersection;
    bool            mIsBooleanQuery;
    vector<string>    mQueryTerms;
    M6Token            mLookahead;
};

M6QueryParser::M6QueryParser(M6Databank* inDatabank, const string& inQuery, bool inAllTermsRequired)
    : mDatabank(inDatabank), mTokenizer(inQuery), mImplicitIntersection(inAllTermsRequired), mIsBooleanQuery(false)
{
}

M6Token M6QueryParser::GetNextToken()
{
    return mTokenizer.GetNextQueryToken();
}

void M6QueryParser::Match(M6Token inToken)
{
    if (mLookahead != inToken)
    {
        boost::format fmt("expected %1% but found %2%");
        THROW(((fmt % inToken % mLookahead).str().c_str()));
    }

    mLookahead = GetNextToken();
}

void M6QueryParser::Parse(vector<string>& outTerms, M6Iterator*& outFilter)
{
    outFilter = nullptr;
    outTerms.clear();

    mLookahead = GetNextToken();
    outFilter = ParseQuery();
    if (mLookahead != eM6TokenEOF)
        THROW(("Parse error"));
    swap(outTerms, mQueryTerms);
}

M6Iterator* M6QueryParser::ParseQuery()
{
    unique_ptr<M6Iterator> result(ParseTest());

    for (;;)
    {
        if (mLookahead == eM6TokenEOF or mLookahead == eM6TokenCloseParenthesis)
            break;

        switch (mLookahead)
        {
            case eM6TokenAND:
                mIsBooleanQuery = true;
                Match(mLookahead);
                result.reset(M6IntersectionIterator::Create(result.release(), ParseTest()));
                break;

            case eM6TokenOR:
                mIsBooleanQuery = true;
                Match(mLookahead);
                result.reset(M6UnionIterator::Create(result.release(), ParseTest()));
                break;

            default:
                if (mImplicitIntersection)
                {
                    result.reset(M6IntersectionIterator::Create(result.release(), ParseTest()));
                }
                else
                {
                    result.reset(M6UnionIterator::Create(result.release(), ParseTest()));
                }
                break;
        }
    }

    return result.release();
}

M6Iterator* M6QueryParser::ParseTest()
{
    unique_ptr<M6Iterator> result;

    switch (mLookahead)
    {
        case eM6TokenOpenBracket:
            Match(eM6TokenOpenBracket);
            result.reset(ParseLink());
            Match(eM6TokenCloseBracket);
            break;

        case eM6TokenOpenParenthesis:
            Match(eM6TokenOpenParenthesis);
            result.reset(ParseQuery());
            Match(eM6TokenCloseParenthesis);
            break;

        case eM6TokenNOT:
        {

            Match(eM6TokenNOT);
            mIsBooleanQuery = true;

            vector<string> queryterms(mQueryTerms);

            if (mDatabank != nullptr)
                result.reset(new M6NotIterator(ParseQuery(), mDatabank->GetMaxDocNr()));

            mQueryTerms = queryterms;
            break;
        }

        case eM6TokenDocNr:
            result.reset(new M6SingleDocIterator(boost::lexical_cast<uint32>(mTokenizer.GetTokenString())));
            Match(eM6TokenDocNr);
            break;

        case eM6TokenString:
        {
            M6Tokenizer tokenizer(mTokenizer.GetTokenValue(), mTokenizer.GetTokenLength());
            for (;;)
            {
                M6Token token = tokenizer.GetNextWord();
                if (token == eM6TokenEOF)
                    break;

                if (token == eM6TokenWord or token == eM6TokenNumber or token == eM6TokenFloat)
                    mQueryTerms.push_back(tokenizer.GetTokenString());
            }

            if (mDatabank != nullptr)
                result.reset(mDatabank->FindString("*", mTokenizer.GetTokenString()));

            Match(eM6TokenString);
            break;
        }

        case eM6TokenPattern:
        {
            string pat = mTokenizer.GetTokenString();
            Match(eM6TokenPattern);
            if (mLookahead == eM6TokenColon and pat == "*")
            {
                Match(eM6TokenColon);
                result.reset(ParseTest());
            }
            else if (mDatabank != nullptr)
            {
                if (pat == "*")
                    result.reset(new M6AllDocIterator(mDatabank->size()));
                else
                    result.reset(mDatabank->FindPattern("full-text", pat));
            }
            break;
        }

        case eM6TokenWord:
        case eM6TokenNumber:
        case eM6TokenFloat:
        {
            string s = mTokenizer.GetTokenString();

            Match(mLookahead);

            if (mLookahead >= eM6TokenColon and mLookahead <= eM6TokenGreaterThan)
            {
                result.reset(ParseQualifiedTest(s));
            }
            else if (mLookahead == eM6TokenBETWEEN)
            {
                result.reset(ParseBetween(s));
            }
            else if (mLookahead == eM6TokenPunctuation)
            {
                mQueryTerms.push_back(s);

                do
                {
                    string punct = mTokenizer.GetTokenString();

                    Match(eM6TokenPunctuation);

                    if (mLookahead != eM6TokenWord and mLookahead != eM6TokenNumber and mLookahead != eM6TokenFloat)
                        break;

                    mQueryTerms.push_back(mTokenizer.GetTokenString());
                    s = s + punct + mQueryTerms.back();
                    Match(mLookahead);
                }
                while (mLookahead == eM6TokenPunctuation);

                if (mDatabank != nullptr)
                {
                    if (mQueryTerms.size() > 1)
                        result.reset(mDatabank->FindString("*", s));
                    else
                        result.reset(mDatabank->Find("*", mQueryTerms.front()));
                }
            }
            else
            {
                if (mDatabank != nullptr)
                    result.reset(mDatabank->Find("*", s));
                mQueryTerms.push_back(s);
            }
            break;
        }

        default:// force an exception
            Match(eM6TokenWord);
    }

    return result.release();
}

M6Iterator* M6QueryParser::ParseQualifiedTest(const string& inIndex)
{
    unique_ptr<M6Iterator> result;

    switch (mLookahead)
    {
        case eM6TokenColon:
            mIsBooleanQuery = true;
            Match(eM6TokenColon);
            result.reset(ParseTerm(inIndex));
            break;

        case eM6TokenEquals:
            mIsBooleanQuery = true;
            Match(eM6TokenEquals);
            result.reset(ParseTerm(inIndex));
            break;

        case eM6TokenLessThan:
            mIsBooleanQuery = true;
            Match(eM6TokenLessThan);
            result.reset(ParseBooleanTerm(inIndex, eM6LessThan));
            break;

        case eM6TokenLessEqual:
            mIsBooleanQuery = true;
            Match(eM6TokenLessEqual);
            result.reset(ParseBooleanTerm(inIndex, eM6LessOrEqual));
            break;

        case eM6TokenGreaterEqual:
            mIsBooleanQuery = true;
            Match(eM6TokenGreaterEqual);
            result.reset(ParseBooleanTerm(inIndex, eM6GreaterOrEqual));
            break;

        case eM6TokenGreaterThan:
            mIsBooleanQuery = true;
            Match(eM6TokenGreaterThan);
            result.reset(ParseBooleanTerm(inIndex, eM6GreaterThan));
            break;

        default:
            THROW(("relational operators are unsupported for now"));
    }

    return result.release();
}

M6Iterator* M6QueryParser::ParseBetween(const string& inIndex)
{
    mIsBooleanQuery = true;

    Match(eM6TokenBETWEEN);

    string lowerbound = mTokenizer.GetTokenString();

    if (mLookahead == eM6TokenString or mLookahead == eM6TokenWord or mLookahead == eM6TokenFloat)
        Match(mLookahead);
    else
        Match(eM6TokenNumber);

    Match(eM6TokenAND);

    string upperbound = mTokenizer.GetTokenString();

    if (mLookahead == eM6TokenString or mLookahead == eM6TokenWord or mLookahead == eM6TokenFloat)
        Match(mLookahead);
    else
        Match(eM6TokenNumber);

    M6Iterator* result = nullptr;
    if (mDatabank != nullptr)
        result = mDatabank->Find(inIndex, lowerbound, upperbound);
    return result;
}

M6Iterator* M6QueryParser::ParseLink()
{
    unique_ptr<M6UnionIterator> result(new M6UnionIterator);

    while (mLookahead != eM6TokenCloseBracket)
    {
        string db = mTokenizer.GetTokenString();
        Match(eM6TokenWord);
        Match(eM6TokenSlash);
        switch (mLookahead)
        {
            case eM6TokenDocNr:
                if (mDatabank != nullptr)
                    result->AddIterator(GetLinks(db, boost::lexical_cast<uint32>(mTokenizer.GetTokenString())));
                Match(eM6TokenDocNr);
                break;

            case eM6TokenWord:
            case eM6TokenNumber:
            case eM6TokenFloat:
                if (mDatabank != nullptr)
                    result->AddIterator(GetLinks(db, mTokenizer.GetTokenString()));
                Match(mLookahead);
                break;

            default:
                Match(eM6TokenWord);
        }
    }

    return result.release();
}

M6Iterator* M6QueryParser::ParseTerm(const string& inIndex)
{
    unique_ptr<M6Iterator> result;

    switch (mLookahead)
    {
        case eM6TokenString:
        {
            M6Tokenizer tokenizer(mTokenizer.GetTokenValue(), mTokenizer.GetTokenLength());
            for (;;)
            {
                M6Token token = tokenizer.GetNextWord();
                if (token == eM6TokenEOF)
                    break;

                if (token == eM6TokenWord or token == eM6TokenNumber or token == eM6TokenFloat)
                    mQueryTerms.push_back(tokenizer.GetTokenString());
            }

            if (mDatabank != nullptr)
                result.reset(mDatabank->FindString(inIndex, mTokenizer.GetTokenString()));

            Match(eM6TokenString);
            break;
        }

        case eM6TokenPattern:
            if (mDatabank != nullptr)
                result.reset(mDatabank->FindPattern(inIndex, mTokenizer.GetTokenString()));
            Match(eM6TokenPattern);
            break;

        case eM6TokenWord:
        case eM6TokenNumber:
        case eM6TokenFloat:
            if (mDatabank != nullptr)
                result.reset(mDatabank->Find(inIndex, mTokenizer.GetTokenString()));
            Match(mLookahead);
            break;

        default:
            Match(eM6TokenWord);
            break;
    }

    return result.release();
}

M6Iterator* M6QueryParser::ParseBooleanTerm(const string& inIndex, M6QueryOperator inOperator)
{
    unique_ptr<M6Iterator> result;

    switch (mLookahead)
    {
        case eM6TokenString:
        {
            M6Tokenizer tokenizer(mTokenizer.GetTokenValue(), mTokenizer.GetTokenLength());
            for (;;)
            {
                M6Token token = tokenizer.GetNextWord();
                if (token == eM6TokenEOF)
                    break;

                if (token == eM6TokenWord or token == eM6TokenNumber or token == eM6TokenFloat)
                    mQueryTerms.push_back(tokenizer.GetTokenString());
            }

            if (mDatabank != nullptr)
                result.reset(mDatabank->Find(inIndex, mTokenizer.GetTokenString(), inOperator));

            Match(eM6TokenString);
            break;
        }

        case eM6TokenWord:
        case eM6TokenNumber:
        case eM6TokenFloat:
            if (mDatabank != nullptr)
                result.reset(mDatabank->Find(inIndex, mTokenizer.GetTokenString(), inOperator));
            Match(mLookahead);
            break;

        default:
            Match(eM6TokenNumber);
            break;
    }

    return result.release();
}

M6Iterator* M6QueryParser::GetLinks(const string& inDB, const string& inDocID)
{
    return mDatabank->GetLinkedDocuments(inDB, inDocID);
}

M6Iterator* M6QueryParser::GetLinks(const string& inDB, uint32 inDocNr)
{
    return mDatabank->GetLinkedDocuments(inDB, to_string(inDocNr));
}

// --------------------------------------------------------------------

void AnalyseQuery(const string& inQuery, vector<string>& outTerms)
{
    M6QueryParser parser(nullptr, inQuery, true);

    M6Iterator* filter = nullptr;

    try
    {
        parser.Parse(outTerms, filter);
    }
    catch (...)
    {
        outTerms.clear();
    }

    delete filter;
}

void ParseQuery(M6Databank& inDatabank, const string& inQuery,
    bool inAllTermsRequired, vector<string>& outTerms, M6Iterator*& outFilter,
    bool& outIsBooleanQuery)
{
    M6QueryParser parser(&inDatabank, inQuery, inAllTermsRequired);
    try
    {
        parser.Parse(outTerms, outFilter);
        outIsBooleanQuery = parser.IsBooleanQuery();
    }
    catch (...)
    {
        delete outFilter;
        throw;
    }
}

