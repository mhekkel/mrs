#include "M6Lib.h"

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>


#include "M6Document.h"
#include "M6Tokenizer.h"
#include "M6Error.h"
//#include "M6FastLZ.h"

using namespace std;
namespace io = boost::iostreams;

M6Document::M6Document(M6Databank& inDatabank)
	: mDatabank(inDatabank)
{
}

M6Document::~M6Document()
{
	
}

//void M6Document::SetText(const string& inText)
//{
//	mText = inText;
//}
//
//const string& M6Document::GetText() const
//{
//	return mText;
//}
//
//void M6Document::SetAttribute(const string& inName, const string& inData)
//{
//	if (inData.length() > 255)
//		THROW(("Length of attribute %s is too large (limit is 255)", inName.c_str()));
//	
//	mAttributes[inName] = inData;
//}
//
//string M6Document::GetAttribute(const string& inName)
//{
//	return mAttributes[inName];
//}
//
//void M6Document::Compress(vector<uint8>& outData) const
//{
//	// set-up the compression machine
//	io::zlib_params params(io::zlib::best_speed);
//	params.noheader = true;
//	params.calculate_crc = true;
//	
//	io::zlib_compressor z_stream(params);
//	
//	io::filtering_stream<io::output> out;
//	out.push(z_stream);
//	out.push(io::back_inserter(outData));
//	
//	out << mText;
//}
//
//void M6Document::Decompress(const vector<char>& inData)
//{
//	// set-up the decompression machine
//	io::zlib_params params;
//	params.noheader = true;
//	params.calculate_crc = true;
//	
//	io::zlib_decompressor z_stream(params);
//	
//	io::stream<io::array_source> in(&inData[0], inData.size());
//	
//	io::filtering_stream<io::input> is;
//	is.push(z_stream);
//	is.push(in);
//	
//	is >> mText;
//}
//
////M6IndexTokenList::iterator M6Document::GetIndexTokens(
////	const std::string& inIndex, M6IndexKind inIndexKind)
////{
////	if (inIndexName.empty())
////		THROW(("Empty index name"));
////	
////	if (inIndexName.length() > 31)
////	{
////		cerr << "Index name is limited to 31 characters, truncating (\""
////			 << inIndexName << "\")" << endl;
////		inIndexName.erase(inIndexName.begin() + 31, inIndexName.end());
////	}
////	
////	M6IndexTokenList::iterator result = mTokens.begin();
////	while (result != mTokens.end() and result->mIndexName != inIndexName)
////		++result;
////	
////	if (result == mTokens.end())
////	{
////		M6IndexTokens tokens = { inIndexKind, inIndexName };
////		mTokens.push_back(tokens);
////		result = mTokens.end() - 1;
////	}
////	else if (result->mIndexKind != inIndexKind)
////		THROW(("Inconsistent use of indices for index %s", inIndexName.c_str()));
////	
////	return result;
////}
//
//void M6Document::IndexText(const string& inIndex,
//	M6IndexKind inIndexKind, const string& inText, bool inIndexNumbers)
//{
////	M6IndexTokens::iterator ix = GetIndexTokens(inIndex, inIndexKind);
////	
////	M6Tokenizer tokenizer(inText.c_str(), inText.length());
////	for (;;)
////	{
////		M6Token token = tokenizer.GetToken();
////		if (token == eM6TokenEOF)
////			break;
////		
////		M6TokenData t = {};
////		
////		uint32 l = tokenizer.GetTokenLength();
////		if (l == 0)
////			continue;
////		
////		if ((token == eM6TokenNumber and not inIndexNumbers) or
////			token == eM6TokenPunctuation or
////			l > kM6MaxKeyLength)
////		{
////			ix->mTokens.push_back(t);
////			continue;
////		}
////		
////		if (token != eM6TokenNumber and token != eM6TokenWord)
////			continue;
////		
////		t.mDocToken = mDocLexicon.Store(tokenizer.GetTokenValue(), l);
////		
////		ix->mTokens.push_back(t);
////	}
//}
//
//void M6Document::Tokenize(M6Lexicon& inLexicon)
//{
//	
//}
//
