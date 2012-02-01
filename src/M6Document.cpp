#include "M6Lib.h"

#include "M6Document.h"
#include "M6Tokenizer.h"
#include "M6Error.h"

using namespace std;

M6Document::M6Document()
{
	
}

M6Document::~M6Document()
{
	
}

void M6Document::Compress(vector<uint8>& outData) const
{
	copy(mText.begin(), mText.end(), back_inserter(outData));
}

void M6Document::Decompress(const vector<uint8>& inData)
{
	mText.assign(reinterpret_cast<const char*>(inData[0]), inData.size());
}

//M6IndexTokenList::iterator M6Document::GetIndexTokens(
//	const std::string& inIndex, M6IndexKind inIndexKind)
//{
//	if (inIndexName.empty())
//		THROW(("Empty index name"));
//	
//	if (inIndexName.length() > 31)
//	{
//		cerr << "Index name is limited to 31 characters, truncating (\""
//			 << inIndexName << "\")" << endl;
//		inIndexName.erase(inIndexName.begin() + 31, inIndexName.end());
//	}
//	
//	M6IndexTokenList::iterator result = mTokens.begin();
//	while (result != mTokens.end() and result->mIndexName != inIndexName)
//		++result;
//	
//	if (result == mTokens.end())
//	{
//		M6IndexTokens tokens = { inIndexKind, inIndexName };
//		mTokens.push_back(tokens);
//		result = mTokens.end() - 1;
//	}
//	else if (result->mIndexKind != inIndexKind)
//		THROW(("Inconsistent use of indices for index %s", inIndexName.c_str()));
//	
//	return result;
//}

void M6Document::IndexText(const string& inIndex,
	M6IndexKind inIndexKind, const string& inText, bool inIndexNumbers)
{
//	M6IndexTokens::iterator ix = GetIndexTokens(inIndex, inIndexKind);
//	
//	M6Tokenizer tokenizer(inText.c_str(), inText.length());
//	for (;;)
//	{
//		M6Token token = tokenizer.GetToken();
//		if (token == eM6TokenEOF)
//			break;
//		
//		M6TokenData t = {};
//		
//		uint32 l = tokenizer.GetTokenLength();
//		if (l == 0)
//			continue;
//		
//		if ((token == eM6TokenNumber and not inIndexNumbers) or
//			token == eM6TokenPunctuation or
//			l > kM6MaxKeyLength)
//		{
//			ix->mTokens.push_back(t);
//			continue;
//		}
//		
//		if (token != eM6TokenNumber and token != eM6TokenWord)
//			continue;
//		
//		t.mDocToken = mDocLexicon.Store(tokenizer.GetTokenValue(), l);
//		
//		ix->mTokens.push_back(t);
//	}
}

void M6Document::Tokenize(M6Lexicon& inLexicon)
{
	
}
