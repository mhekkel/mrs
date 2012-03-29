#include "M6Lib.h"

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
	
	void			Parse(vector<string>& outTerms, M6Iterator*& outFilter);

  private:
	M6Iterator*		ParseQuery();
	M6Iterator*		ParseTest();
	M6Iterator*		ParseQualifiedTest(const string& inIndex);
	M6Iterator*		ParseTerm(const string& inIndex);
	M6Iterator*		ParseString();

	M6Token			GetNextToken();
	void			Match(M6Token inToken);
	
	M6Databank*		mDatabank;
	M6Tokenizer		mTokenizer;
	bool			mImplicitIntersection;
	bool			mIsBooleanQuery;
	vector<string>	mQueryTerms;
	M6Token			mLookahead;
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
		boost::format fmt("Query error: expected %1% but found %2%");
		THROW(((fmt % inToken % mLookahead).str().c_str()));
	}
	
	mLookahead = GetNextToken();
}

void M6QueryParser::Parse(vector<string>& outTerms, M6Iterator*& outFilter)
{
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
					result.reset(M6IntersectionIterator::Create(result.release(), ParseTest()));
				else
					result.reset(M6UnionIterator::Create(result.release(), ParseTest()));
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
		case eM6TokenOpenParenthesis:
			Match(eM6TokenOpenParenthesis);
			result.reset(ParseQuery());
			Match(eM6TokenCloseParenthesis);
			break;
		
		case eM6TokenNOT:
			mIsBooleanQuery = true;
			if (mDatabank != nullptr)
				result.reset(new M6NotIterator(ParseQuery(), mDatabank->GetMaxDocNr()));
			break;
		
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
					
				if (token == eM6TokenWord or token == eM6TokenNumber)
					mQueryTerms.push_back(tokenizer.GetTokenString());
			}

			if (mDatabank != nullptr)
				result.reset(mDatabank->FindString("*", mTokenizer.GetTokenString()));

			Match(eM6TokenString);
			break;
		}

		case eM6TokenWord:
		{
			string s = mTokenizer.GetTokenString();
			Match(eM6TokenWord);
			
			if (mLookahead >= eM6TokenColon and mLookahead <= eM6TokenGreaterThan)
				result.reset(ParseQualifiedTest(s));
			else
				mQueryTerms.push_back(s);
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
		
		default:
			THROW(("relational operators are unsupported for now"));
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
					
				if (token == eM6TokenWord or token == eM6TokenNumber)
					mQueryTerms.push_back(tokenizer.GetTokenString());
			}

			if (mDatabank != nullptr)
				result.reset(mDatabank->FindString(inIndex, mTokenizer.GetTokenString()));

			Match(eM6TokenString);
			break;
		}

		case eM6TokenPattern:
			if (mDatabank != nullptr)
				result.reset(mDatabank->Find(inIndex, mTokenizer.GetTokenString(), true));
			Match(eM6TokenPattern);
			break;
		
		case eM6TokenWord:
		case eM6TokenNumber:
			if (mDatabank != nullptr)
				result.reset(mDatabank->Find(inIndex, mTokenizer.GetTokenString(), false));
			Match(mLookahead);
			break;

		default:
			Match(eM6TokenWord);
			break;
	}
	
	return result.release();
}

// --------------------------------------------------------------------

void AnalyseQuery(const string& inQuery, vector<string>& outTerms)
{
	M6QueryParser parser(nullptr, inQuery, true);
	
	M6Iterator* filter = nullptr;
	parser.Parse(outTerms, filter);
	delete filter;
}

void ParseQuery(M6Databank& inDatabank, const string& inQuery,
	bool inAllTermsRequired, vector<string>& outTerms, M6Iterator*& outFilter)
{
	M6QueryParser parser(&inDatabank, inQuery, inAllTermsRequired);
	parser.Parse(outTerms, outFilter);
}

