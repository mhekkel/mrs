#include "M6Lib.h"

#include "M6Document.h"
#include "M6Tokenizer.h"
#include "M6Error.h"
#include "M6FastLZ.h"

using namespace std;

M6Document::M6Document()
{
	
}

M6Document::~M6Document()
{
	
}

void M6Document::Compress(vector<uint8>& outData) const
{
	size_t l = mText.length() + mText.length() / 20;
	if (l < mText.length() + 5)
		l = mText.length() + 5;
	
	outData = vector<uint8>(l + 4);
	size_t r = FastLZCompress(mText.c_str(), mText.length(),
		&outData[4], l);
	outData.erase(outData.begin() + r + 4, outData.end());

	l = mText.length();
	outData[0] = static_cast<uint8>(l >> 24);
	outData[1] = static_cast<uint8>(l >> 16);
	outData[2] = static_cast<uint8>(l >>  8);
	outData[3] = static_cast<uint8>(l >>  0);
}

void M6Document::Decompress(const vector<uint8>& inData)
{
	uint32 l = 0;
	l = l << 8 | inData[0];
	l = l << 8 | inData[1];
	l = l << 8 | inData[2];
	l = l << 8 | inData[3];
	
	mText.assign(l, 0);
	FastLZDecompress(&inData[4], inData.size() - 4,
		const_cast<char*>(mText.c_str()), l);

//	mText.assign(reinterpret_cast<const char*>(&inData[0]), inData.size());
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
