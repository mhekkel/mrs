#include "M6Lib.h"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

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
					M6QueryParser(M6Databank& inDatabank, const string& inQuery,
						bool inAllTermsRequired);
	
	void			Parse(vector<string>& outTerms, M6Iterator*& outFilter);

  private:
	M6Iterator*		ParseQuery();
	M6Iterator*		ParseTest();
	M6Iterator*		ParseQualifiedTest(const string& inIndex);
	M6Iterator*		ParseTerm(const string& inIndex);

	M6Token			GetNextToken();
	void			Match(M6Token inToken);
	
	M6Databank&		mDatabank;
	M6Tokenizer		mTokenizer;
	bool			mImplicitIntersection;
	bool			mIsBooleanQuery;
	vector<string>	mQueryTerms;
	M6Token			mLookahead;
};

M6QueryParser::M6QueryParser(M6Databank& inDatabank,
	const string& inQuery, bool inAllTermsRequired)
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
	unique_ptr<M6Iterator> result;

	for (;;)
	{
		if (mLookahead == eM6TokenEOF or mLookahead == eM6TokenCloseParenthesis)
			break;
		
		switch (mLookahead)
		{
			case eM6TokenAND:
			case eM6TokenOR:
				mIsBooleanQuery = true;
				Match(mLookahead);
				if (mLookahead == eM6TokenAND)
					result.reset(new M6IntersectionIterator(result.release(), ParseTest()));
				else
					result.reset(new M6UnionIterator(result.release(), ParseTest()));
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
		{
			Match(eM6TokenOpenParenthesis);
			result.reset(ParseQuery());
			Match(eM6TokenCloseParenthesis);
			break;
		}
			
		case eM6TokenNOT:
			mIsBooleanQuery = true;
			result.reset(new M6NotIterator(ParseQuery()));
			break;

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
		
//		case eM6Token
		
	}
	
	return result.release();	
}

M6Iterator* M6QueryParser::ParseTerm(const string& inIndex)
{
	string term = mTokenizer.GetTokenString();
	Match(eM6TokenWord);
	return mDatabank.Find(inIndex, term, false);
}

// --------------------------------------------------------------------

void ParseQuery(M6Databank& inDatabank, const string& inQuery,
	bool inAllTermsRequired, vector<string>& outTerms, M6Iterator*& outFilter)
{
	M6QueryParser parser(inDatabank, inQuery, inAllTermsRequired);
	parser.Parse(outTerms, outFilter);
}

