#include "M6Lib.h"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "M6Query.h"
#include "M6Tokenizer.h"
#include "M6Iterator.h"

using namespace std;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

enum M6QueryToken
{
	eM6QUndefined	= -1,
	
	eM6QOpenParenthesis = '(',
	eM6QColon = ':',
	
	eM6QEnd		=	256,
	eM6QString,
	eM6QLiteral,
	eM6QNumber,
	eM6QIdent,
	eM6QPattern,
	eM6QDocNr,
	
	eM6QAND,
	eM6QOR,
	eM6QNOT,
	
	eM6QLT,
	eM6QLE,
	eM6QEQ,
	eM6QGE,
	eM6QGT
};

ostream& operator<<(ostream& os, M6QueryToken inToken)
{
	switch (inToken)
	{
		case eM6QEnd:		os << "end of query"; 	break;
		case eM6QString:	os << "string";			break;
		case eM6QLiteral:	os << "literal";		break;
		case eM6QNumber:	os << "number"; 		break;
		case eM6QIdent:		os << "identifier"; 	break;
		case eM6QPattern:	os << "pattern";		break;
		case eM6QDocNr:		os << "document number";break;
		case eM6QAND:		os << "AND";			break;
		case eM6QOR:		os << "OR";				break;
		case eM6QNOT:		os << "NOT";			break;
		case eM6QLT:		os << "'<='";			break;
		case eM6QLE:		os << "'<'";			break;
		case eM6QEQ:		os << "'='";			break;
		case eM6QGE:		os << "'>='";			break;
		case eM6QGT:		os << "'>='";			break;

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

enum M6QueryOperator : char
{
	eM6QContains	= ':',
	eM6QEquals		= '=',
	eM6QLessThan	= '<',
	eM6QGreaterThan	= '>',
};

struct M6QueryParser
{
						M6QueryParser(M6Databank& inDatabank);

	
	
	M6Iterator*			Parse(const string& inQuery, bool inAllTermsRequired);

	M6QueryToken		GetNextToken();
	void				Match(M6QueryToken inToken);
	
	M6Iterator*			CreateIterator(const string& inIndex,
							const string& inValue, M6QueryOperator inOp);

	bool				mImplicitIntersection;
	bool				mIsBooleanQuery;
};

M6QueryToken M6QueryParser::GetNextToken()
{
	
}

void M6QueryParser::Match(M6QueryToken inToken)
{
	if (mLookahead != inToken)
	{
		boost::format fmt("Query error: expected %1% but found %2%");
		THROW(((fmt % inToken % mLookahead).str()));
	}
	
	mLookahead = GetNextToken();
}

M6Iterator* M6QueryParser::Parse(const string& inQuery)
{
	unique_ptr<M6Iterator> result;
	
	if (inQuery == "*")
		result.reset(new M6AllIterator(mDatabank.size()));
	else
	{
		mLookahead = GetNextToken();
		
		while (mLookahead != eM6QEnd)
		{
			switch (mLookahead)
			{
				case eM6QOR:
				case eM6QAND:
					mIsBooleanQuery = true;
					Match(mLookahead);
					if (mLookahead == eM6QAND)
						result.reset(new M6IntersectionIterator(result.release(), ParseTest()));
					else
						result.reset(new M6UnionIterator(result.release(), ParseTest()));
					break;
				
				default:
					if (mImplicitIntersection)
						result.reset(new M6IntersectionIterator(result.release(), ParseTest()));
					else
						result.reset(new M6UnionIterator(result.release(), ParseTest()));
					break;
			}
		}
	}
	
	return result.release();
}

M6Iterator* M6QueryParser::ParseSubQuery()
{
	mIsBooleanQuery = true;
	Match('(');

	unique_ptr<M6Iterator> result = ParseTest();

	while (mLookahead != ')')
	{
		switch (mLookahead)
		{
			case eM6QOR:
			case eM6QAND:
				Match(mLookahead);
				if (mLookahead == eM6QAND)
					result.reset(new M6IntersectionIterator(result.release(), ParseTest()));
				else
					result.reset(new M6UnionIterator(result.release(), ParseTest()));
				break;
			
			default:
				if (mImplicitIntersection)
					result.reset(new M6IntersectionIterator(result.release(), ParseTest()));
				else
					result.reset(new M6UnionIterator(result.release(), ParseTest()));
				break;
		}
	}
	Match(')');
	
	return result.release();
}

M6Iterator* M6QueryParser::ParseTest()
{
	unique_ptr<M6Iterator> result;
	
	switch (mLookahead)
	{
		case '(':		result.reset(ParseSubQuery()); break;
		case eM6QNOT:	mIsBooleanQuery = true; result.reset(M6NotIterator(ParseTest())); break;
		
	}
}

M6Iterator* M6QueryParser::CreateIterator(
	const string& inIndex, const string& inValue, M6QueryOperator inOp)
{
	M6Iterator* result;
	
	if (inIndex == "*")
		result = CreateIteratorForTerm(inIndex, inValue,
			ba::contains(inValue, "*") or ba::contains(inValue, "?"));
	else
		result = CreateIteratorForOperator(inIndex, inValue, inOp);
	
	return result;
}


// --------------------------------------------------------------------

void ParseQuery(M6Databank& inDatabank, const string& inQuery,
	bool inAllTermsRequired,
	vector<string>& outQueryTerms, M6Iterator*& outIterator)
{
	
}

