//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <iostream>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/algorithm/string.hpp>
//#include <boost/locale.hpp>
#include <boost/range/sub_range.hpp>

#include "M6Document.h"
#include "M6DocStore.h"
#include "M6Tokenizer.h"
#include "M6Error.h"
//#include "M6FastLZ.h"
#include "M6Databank.h"
#include "M6Index.h"

#include "M6Log.h"

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

M6InputDocument::M6InputDocument(M6Databank& inDatabank)
    : M6Document(inDatabank)
    , mDocNr(inDatabank.GetDocStore().GetNextDocumentNumber())
{
}

M6InputDocument::M6InputDocument(M6Databank& inDatabank, const string& inText)
    : M6Document(inDatabank)
    , mText(inText)
    , mDocNr(inDatabank.GetDocStore().GetNextDocumentNumber())
{
}

void M6InputDocument::SetText(const string& inText)
{
    mText = inText;
}

string M6InputDocument::GetText()
{
    return mText;
}

string M6InputDocument::GetAttribute(const string& inName)
{
    return mAttributes[inName];
}

void M6InputDocument::SetAttribute(const string& inName, const char* inText, size_t inSize)
{
    if (inName.length() > 255)
        THROW(("Attribute names are limited to 255 characters"));

    if (inSize > 255)
    {
        if (VERBOSE)
            cerr << "Attribute values are limited to 255 characters" << endl;
        inSize = 255;
    }

    string attribute(inText, inSize);
    ba::trim(attribute);

    mAttributes[inName] = attribute;
}

void M6InputDocument::Compress()
{
    M6DocStore& store(mDatabank.GetDocStore());

    mBuffer.reserve(mText.length() + mText.length() / 20);

    // set-up the compression machine
    io::zlib_params params(io::zlib::best_speed);
    params.noheader = true;
    params.calculate_crc = true;

    io::zlib_compressor z_stream(params);

    io::filtering_stream<io::output> out;
    out.push(z_stream);
    out.push(io::back_inserter(mBuffer));

    for (auto attr : mAttributes)
    {
        uint8 attrNr = store.RegisterAttribute(attr.first);
        out.write(reinterpret_cast<char*>(&attrNr), 1);

        uint8 size = static_cast<uint8>(attr.second.length());
        out.write(reinterpret_cast<char*>(&size), 1);

        out.write(attr.second.c_str(), size);
    }

    char mark = 0;
    out.write(&mark, 1);

    // write links

    if (not mLinks.empty() or ba::starts_with(mText, "[[\n"))
    {
        out << "[[" << endl;
        stringstream ls;

        for (auto& l : mLinks)
        {
            out << l.first << '\t';
            for (auto id : l.second)
                out << id << ';';
            out << endl;
        }

        out << "]]" << endl;
    }

    out.write(mText.c_str(), mText.length());
}

void M6InputDocument::Store()
{
    assert(not mBuffer.empty());
    mDatabank.GetDocStore().StoreDocument(mDocNr, &mBuffer[0], mBuffer.size(), mText.length());
}

M6InputDocument::M6IndexTokenList::iterator M6InputDocument::GetIndexTokens(
    const string& inIndex, M6DataType inDataType)
{
    if (inIndex.empty())
        THROW(("Empty index name"));

    M6IndexTokenList::iterator result = mTokens.begin();
    while (result != mTokens.end() and result->mIndexName != inIndex)
        ++result;

    if (result == mTokens.end())
    {
        M6IndexTokens tokens = { inDataType, inIndex };
        mTokens.push_back(tokens);
        result = mTokens.end() - 1;
    }
    else if (result->mDataType != inDataType)
        THROW(("Inconsisted datatype for index %s", inIndex.c_str()));

    return result;
}

void M6InputDocument::Index(const string& inIndex, M6DataType inDataType,
    bool isUnique, const char* inText, size_t inSize)
{
    bool tokenize = true;
//#pragma message("TODO implement inIndexNrs")
    bool inIndexNumbers = true;

    if (inDataType != eM6TextData)
    {
        tokenize = inDataType == eM6StringData;
        M6IndexValue v = { inDataType, inIndex, string(inText, inSize), 0, isUnique };

        if (inDataType == eM6StringData)
            M6Tokenizer::CaseFold(v.mIndexValue);
        mValues.push_back(v);
    }

    if (tokenize)
    {
        auto ix = GetIndexTokens(inIndex, inDataType);

        M6Tokenizer tokenizer(inText, inSize);
        for (;;)
        {
            M6Token token = tokenizer.GetNextWord();
            if (token == eM6TokenEOF)
                break;

            size_t l = tokenizer.GetTokenLength();
#if DEBUG
            if (l == 0)
            {
                cerr << endl
                     << "l == 0, text = '" << string(inText, inSize) << '\'' << endl
                     << " tokentext = '" << tokenizer.GetTokenValue() << '\'' << endl
                     << " token = " << token << endl;

            }
#endif
            assert(l > 0);

            if (((token == eM6TokenNumber or token == eM6TokenFloat) and not inIndexNumbers) or
                token == eM6TokenPunctuation or
                l > kM6MaxKeyLength)
            {
                if (ix->mTokens.empty() or ix->mTokens.back() != 0)
                    ix->mTokens.push_back(0);        // acts as stop word
                continue;
            }

            if (token != eM6TokenNumber and token != eM6TokenFloat and token != eM6TokenWord)
                continue;

            uint32 t = mDocLexicon.Store(tokenizer.GetTokenValue(), l);
            ix->mTokens.push_back(t);
        }
    }
}

void M6InputDocument::Index(const string& inIndex,
        const vector<pair<const char*,size_t>>& inWords)
{
    auto ix = GetIndexTokens(inIndex, eM6StringData);

    for (auto word : inWords)
    {
        uint32 t = mDocLexicon.Store(word.first, word.second);
        ix->mTokens.push_back(t);
    }
}

//void M6InputDocument::IndexSequence(const string& inIndex, uint32 inWordSize,
//    const char* inSequence, size_t inLength)
//{
//    auto ix = GetIndexTokens(inIndex, eM6TextData);
//
//    for (uint32 i = 0; i + inWordSize <= inLength; ++i)
//    {
//        uint32 t = mDocLexicon.Store(inSequence++, inWordSize);
//        ix->mTokens.push_back(t);
//    }
//}

void M6InputDocument::AddLink(const string& inDatabank, const string& inValue)
{
    string db(inDatabank);
    M6Tokenizer::CaseFold(db);

    string id(inValue);
    M6Tokenizer::CaseFold(id);

    mLinks[db].insert(id);
}

void M6InputDocument::Tokenize(M6Lexicon& inLexicon, uint32 inLastStopWord)
{
    uint32 docTokenCount = mDocLexicon.Count();
    vector<uint32> tokenRemap(docTokenCount, kUndefinedTokenValue);
    tokenRemap[0] = 0;

    {
        M6Lexicon::M6SharedLock sharedLock(inLexicon);

        for (uint32 t = 1; t < docTokenCount; ++t)
        {
            const char* w;
            size_t wl;

            mDocLexicon.GetString(t, w, wl);
            uint32 rt = inLexicon.Lookup(w, wl);

            if (rt != 0)
            {
                if (rt <= inLastStopWord)
                    tokenRemap[t] = 0;
                else
                    tokenRemap[t] = rt;
            }
        }
    }

    {
        M6Lexicon::M6UniqueLock uniqueLock(inLexicon);

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
    }

    RemapTokens(&tokenRemap[0]);
}

void M6InputDocument::RemapTokens(const uint32 inTokenMap[])
{
    for (M6IndexTokens& tm : mTokens)
    {
        for (uint32& t : tm.mTokens)
            t = inTokenMap[t];
    }
}

// --------------------------------------------------------------------

M6OutputDocument::M6OutputDocument(M6Databank& inDatabank,
        uint32 inDocNr, uint32 inDocPage, uint32 inDocSize)
    : M6Document(inDatabank)
    , mDocNr(inDocNr)
    , mDocPage(inDocPage)
    , mDocSize(inDocSize)
    , mLinksRead(false)
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

    getline(is, text);
    if (text == "[[")
    {
        do getline(is, text); while (text != "]]" and not is.eof());
        text.clear();
    }
    else
        text += '\n';

    io::filtering_ostream out(io::back_inserter(text));
    io::copy(is, out);

    return text;
}

string M6OutputDocument::GetAttribute(const string& inName)
{
    M6DocStore& store(mDatabank.GetDocStore());

    string result;

    uint8 attrNr = store.RegisterAttribute(inName);
    if (attrNr == 0 and inName == "id")
        result = to_string(mDocNr);
    else
    {
        // set-up the decompression machine
        io::zlib_params params;
        params.noheader = true;
        params.calculate_crc = true;

        io::zlib_decompressor z_stream(params);

        io::filtering_stream<io::input> is;
        is.push(z_stream);
        store.OpenDataStream(mDocNr, mDocPage, mDocSize, is);

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
    }

    return result;
}

M6DocLinks& M6OutputDocument::GetLinks()
{
    if (not mLinksRead)
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

        string line;
        getline(is, line);

        if (line == "[[")
        {
            for (;;)
            {
                getline(is, line);
                if ((line.empty() and is.eof()) or line == "]]")
                    break;

                string::size_type s = line.find('\t');
                if (s == string::npos)
                    continue;

                string db = line.substr(0, s);

                vector<string> ids;
                line.erase(0, s + 1);
                if (ba::ends_with(line, ";"))
                    line.erase(line.end() - 1);
                ba::split(ids, line, ba::is_any_of(";"), ba::token_compress_on);
                mLinks[db].insert(ids.begin(), ids.end());
            }
        }

        mLinksRead = true;
    }

    return mLinks;
}
