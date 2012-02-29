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

ostream& operator<<(ostream& os, M6Token inToken)
{
	switch (inToken)
	{
		case eM6TokenEOF:	os << "end of query"; 	break;
//		case eM6String:		os << "string";			break;
//		case eM6Literal:	os << "literal";		break;
//		case eM6Number:		os << "number"; 		break;
//		case eM6Ident:		os << "identifier"; 	break;
//		case eM6Pattern:	os << "pattern";		break;
//		case eM6DocNr:		os << "document number";break;
//		case eM6AND:		os << "AND";			break;
//		case eM6OR:			os << "OR";				break;
//		case eM6NOT:		os << "NOT";			break;
//		case eM6LT:			os << "'<='";			break;
//		case eM6LE:			os << "'<'";			break;
//		case eM6EQ:			os << "'='";			break;
//		case eM6GE:			os << "'>='";			break;
//		case eM6GT:			os << "'>='";			break;

		default:
		{
			if (inToken == '\'')
				os << "'";
			else if (isprint(inToken))
				os << "'" << char(inToken) << "'";
			else
				os << "token(0x" << ios::hex << int(inToken) << ")";
			break;
		}
	}
	
	return os;
}

class M6QueryParser
{
  public:
					M6QueryParser(M6Databank& inDatabank, const string& inQuery,
						bool inAllTermsRequired);
	
	void			Parse(vector<string>& outTerms, M6Iterator*& outFilter);

  private:
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
	unique_ptr<M6Iterator> result;
	
	mLookahead = GetNextToken();
	
	while (mLookahead != eM6TokenEOF)
	{
		switch (mLookahead)
		{
//			case eM6QOR:
//			case eM6QAND:
//				mIsBooleanQuery = true;
//				Match(mLookahead);
//				if (mLookahead == eM6QAND)
//					result.reset(new M6IntersectionIterator(result.release(), ParseTest()));
//				else
//					result.reset(new M6UnionIterator(result.release(), ParseTest()));
//				break;
			
			default:
				if (mImplicitIntersection)
					result.reset(M6IntersectionIterator::Create(result.release(), ParseTest()));
				else
					result.reset(M6UnionIterator::Create(result.release(), ParseTest()));
				break;
		}
	}
	
	outFilter = result.release();
	swap(outTerms, mQueryTerms);
}

M6Iterator* M6QueryParser::ParseTest()
{
	unique_ptr<M6Iterator> result;
	
	switch (mLookahead)
	{
		//case eM6TokenOpenParenthesis:
		//{
		//	Match(eM6TokenOpenParenthesis);
		//	result.reset(ParseSubQuery());
		//	Match(eM6TokenCloseParenthesis);
		//	break;
		//}
			
//		case eM6QNOT:	mIsBooleanQuery = true; result.reset(M6NotIterator(ParseTest())); break;

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

