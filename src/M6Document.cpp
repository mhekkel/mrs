#include "M6Lib.h"

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>
//#include <boost/locale.hpp>

#include "M6Document.h"
#include "M6DocStore.h"
#include "M6Tokenizer.h"
#include "M6Error.h"
//#include "M6FastLZ.h"
#include "M6Databank.h"
#include "M6Index.h"

using namespace std;
namespace io = boost::iostreams;
namespace ba = boost::algorithm;

namespace
{

const uint32
	kUndefinedTokenValue = ~0;

}

// --------------------------------------------------------------------

M6Document::M6Document(M6Databank& inDatabank)
	: mDatabank(inDatabank)
{
}

M6Document::~M6Document()
{
}

// --------------------------------------------------------------------

M6InputDocument::M6InputDocument(M6Databank& inDatabank, const string& inText)
	: M6Document(inDatabank)
	, mText(inText)
{
}

string M6InputDocument::GetText()
{
	return mText;
}

string M6InputDocument::GetAttribute(const string& inName)
{
	return mAttributes[inName];
}

void M6InputDocument::SetAttribute(const string& inName, const string& inData)
{
	if (inName.length() > 255)
		THROW(("Attribute names are limited to 255 characters"));
	
	if (inData.length() > 255)
		THROW(("Attribute values are limited to 255 characters"));
	
	mAttributes[inName] = inData;
}

uint32 M6InputDocument::Store()
{
	M6DocStore& store(mDatabank.GetDocStore());

	vector<char> buffer;
	buffer.reserve(mText.length() + mText.length() / 20);

	{
		// set-up the compression machine
		io::zlib_params params(io::zlib::best_speed);
		params.noheader = true;
		params.calculate_crc = true;
	
		io::zlib_compressor z_stream(params);

		io::filtering_stream<io::output> out;
		out.push(z_stream);
		out.push(io::back_inserter(buffer));
	
		foreach (auto attr, mAttributes)
		{
			uint8 attrNr = store.RegisterAttribute(attr.first);
			out.write(reinterpret_cast<char*>(&attrNr), 1);
			
			uint8 size = static_cast<uint8>(attr.second.length());
			out.write(reinterpret_cast<char*>(&size), 1);
			
			out.write(attr.second.c_str(), size);
		}
		
		char mark = 0;
		out.write(&mark, 1);
		out.write(mText.c_str(), mText.length());
	}

	mDocNr = store.StoreDocument(&buffer[0], buffer.size());
	return mDocNr;
}

M6InputDocument::M6IndexTokenList::iterator M6InputDocument::GetIndexTokens(
	const std::string& inIndex, M6IndexKind inIndexKind)
{
	if (inIndex.empty())
		THROW(("Empty index name"));
	
	//if (inIndex.length() > 31)
	//{
	//	cerr << "Index name is limited to 31 characters, truncating (\""
	//		 << inIndex<< "\")" << endl;
	//	inIndex.erase(inIndex.begin() + 31, inIndex.end());
	//}
	
	M6IndexTokenList::iterator result = mTokens.begin();
	while (result != mTokens.end() and result->mIndexName != inIndex)
		++result;
	
	if (result == mTokens.end())
	{
		M6IndexTokens tokens = { inIndexKind, inIndex };
		mTokens.push_back(tokens);
		result = mTokens.end() - 1;
	}
	else if (result->mIndexKind != inIndexKind)
		THROW(("Inconsistent use of indices for index %s", inIndex.c_str()));
	
	return result;
}

void M6InputDocument::IndexText(const string& inIndex, M6IndexKind inIndexKind,
	const string& inText, bool inIndexNumbers)
{
	auto ix = GetIndexTokens(inIndex, inIndexKind);
	
	M6Tokenizer tokenizer(inText.c_str(), inText.length());
	for (;;)
	{
		M6Token token = tokenizer.GetToken();
		if (token == eM6TokenEOF)
			break;
		
		uint32 t = 0, l = tokenizer.GetTokenLength();
		if (l == 0)
			continue;
		
		if ((token == eM6TokenNumber and not inIndexNumbers) or
			token == eM6TokenPunctuation or
			l > kM6MaxKeyLength)
		{
			ix->mTokens.push_back(t);
			continue;
		}
		
		if (token != eM6TokenNumber and token != eM6TokenWord)
			continue;
		
		t = mDocLexicon.Store(tokenizer.GetTokenValue(), l);
		
		ix->mTokens.push_back(t);
	}
}

void M6InputDocument::IndexValue(const string& inIndex, M6IndexKind inIndexKind,
	const string& inValue)
{
	auto ix = GetIndexTokens(inIndex, inIndexKind);
	
	string value(inValue);
	ba::trim(value);
	ba::to_lower(value);
	
	if (not value.empty())
		ix->mTokens.push_back(mDocLexicon.Store(value));
}

void M6InputDocument::Tokenize(M6Lexicon& inLexicon, uint32 inLastStopWord)
{
	uint32 docTokenCount = mDocLexicon.Count();
	vector<uint32> tokenRemap(docTokenCount, kUndefinedTokenValue);
	tokenRemap[0] = 0;
	
	try
	{
		inLexicon.LockShared();
		
		for (uint32 t = 1; t < docTokenCount; ++t)
		{
			const char* w;
			size_t wl;
			
			mDocLexicon.GetString(t, w, wl);
			uint32 rt = inLexicon.Lookup(w, wl);

			if (rt != 0)
				tokenRemap[t] = rt;
		}
		
		inLexicon.UnlockShared();
	}
	catch (...)
	{
		inLexicon.UnlockShared();
		throw;
	}
	
	try
	{
		inLexicon.LockUnique();
		
		for (uint32 t = 1; t < docTokenCount; ++t)
		{
			if (tokenRemap[t] == kUndefinedTokenValue)
			{
				const char* w;
				size_t wl;
				
				mDocLexicon.GetString(t, w, wl);
				tokenRemap[t] = inLexicon.Store(w, wl);
			}
		}
		
		inLexicon.UnlockUnique();
	}
	catch (...)
	{
		inLexicon.UnlockUnique();
		throw;
	}
	
	foreach (M6IndexTokens& tm, mTokens)
	{
		foreach (uint32& t, tm.mTokens)
		{
			if (tokenRemap[t] > inLastStopWord)
				t = tokenRemap[t];
			else
				t = 0;

			assert(t != kUndefinedTokenValue);
		}
	}
}

// --------------------------------------------------------------------

M6OutputDocument::M6OutputDocument(M6Databank& inDatabank,
		uint32 inDocNr, uint32 inDocPage, uint32 inDocSize)
	: M6Document(inDatabank)
	, mDocNr(inDocNr)
	, mDocPage(inDocPage)
	, mDocSize(inDocSize)
{
}

string M6OutputDocument::GetText()
{
	M6DocStore& store(mDatabank.GetDocStore());
	
	// set-up the decompression machine
	io::zlib_params params;
	params.noheader = true;
	params.calculate_crc = true;
	
	io::zlib_decompressor z_stream(params);
	
	io::filtering_stream<io::input> is;
	is.push(z_stream);
	store.OpenDataStream(mDocNr, mDocPage, mDocSize, is);
	
	// skip over the attributes first
	
	char c;
	is.read(&c, 1);
	while (c != 0 and not is.eof())
	{
		uint8 l;
		is.read(reinterpret_cast<char*>(&l), 1);
		is.ignore(l);
		is.read(&c, 1);
	}
	
	string text;
	
	io::filtering_ostream out(io::back_inserter(text));
	io::copy(is, out);
	
	return text;
}

string M6OutputDocument::GetAttribute(const string& inName)
{
	M6DocStore& store(mDatabank.GetDocStore());
	
	uint8 attrNr = store.RegisterAttribute(inName);
	
	// set-up the decompression machine
	io::zlib_params params;
	params.noheader = true;
	params.calculate_crc = true;
	
	io::zlib_decompressor z_stream(params);
	
	io::filtering_stream<io::input> is;
	is.push(z_stream);
	store.OpenDataStream(mDocNr, mDocPage, mDocSize, is);
	
	string result;
	
	for (;;)
	{
		char c;
		is.read(&c, 1);

		if (c == 0)
			break;

		uint8 l;
		is.read(reinterpret_cast<char*>(&l), 1);
		
		if (c == attrNr)
		{
			vector<char> buffer(l);
			is.read(&buffer[0], l);
			result.assign(&buffer[0], l);
			break;
		}
		
		is.ignore(l);
	}
	
	return result;
}
