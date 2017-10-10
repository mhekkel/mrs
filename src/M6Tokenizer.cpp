//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <cassert>
#include <cstring>
#include <vector>
#include <iterator>
#include <algorithm>
#include <ostream>
#include <iostream>
#include <tuple>


#include "M6Tokenizer.h"
//#include "M6Unicode.h"
#include "../unicode/M6UnicodeTables.h"
#include "M6Error.h"

using namespace std;

// --------------------------------------------------------------------

ostream& operator<<(ostream& os, M6Token inToken)
{
    switch (inToken)
    {
        case eM6TokenNone:                os << "no token?"; break;
        case eM6TokenEOF:                os << "end of query"; break;
        case eM6TokenUndefined:            os << "undefined token"; break;
        case eM6TokenWord:                os << "word"; break;
        case eM6TokenNumber:            os << "number"; break;
        case eM6TokenFloat:            os << "float"; break;
        case eM6TokenPunctuation:        os << "punctuation character"; break;
        case eM6TokenString:            os << "quoted string"; break;
        case eM6TokenPattern:            os << "glob-pattern (word with * or ?)"; break;
        //case eM6TokenHyphen:            os << "hyphen character"; break;
        //case eM6TokenPlus:                os << "plus character"; break;
        case eM6TokenOR:                os << "OR"; break;
        case eM6TokenAND:                os << "AND"; break;
        case eM6TokenOpenParenthesis:    os << "'('"; break;
        case eM6TokenCloseParenthesis:    os << "')'"; break;
        case eM6TokenOpenBracket:        os << "'['"; break;
        case eM6TokenCloseBracket:        os << "']'"; break;
        case eM6TokenSlash:                os << "'/'"; break;
        case eM6TokenColon:                os << "':'"; break;
        case eM6TokenEquals:            os << "'='"; break;
        case eM6TokenLessThan:            os << "'<'"; break;
        case eM6TokenLessEqual:            os << "'<='"; break;
        case eM6TokenGreaterEqual:        os << "'>='"; break;
        case eM6TokenGreaterThan:        os << "'>'"; break;
        case eM6TokenDocNr:                os << "document number"; break;
        case eM6TokenNOT:                os << "NOT"; break;
    }

    return os;
}

// --------------------------------------------------------------------

template<class InputIterator>
inline
tuple<InputIterator, uint32> ReadUTF8(InputIterator inIterator)
{
    uint32 uc;

    if ((*inIterator & 0x080) == 0)        // next byte is a valid ASCII character
        uc = *inIterator++;
    else    // decode utf-8
    {
        if ((inIterator[0] & 0x0E0) == 0x0C0 and (inIterator[1] & 0x0c0) == 0x080)
        {
            uc = ((inIterator[0] & 0x01F) << 6) | (inIterator[1] & 0x03F);
            inIterator += 2;
        }
        else if ((inIterator[0] & 0x0F0) == 0x0E0 and (inIterator[1] & 0x0c0) == 0x080 and (inIterator[2] & 0x0c0) == 0x080)
        {
            uc = ((inIterator[0] & 0x00F) << 12) | ((inIterator[1] & 0x03F) << 6) | (inIterator[2] & 0x03F);
            inIterator += 3;
        }
        else if ((inIterator[0] & 0x0F8) == 0x0F0 and (inIterator[1] & 0x0c0) == 0x080 and (inIterator[2] & 0x0c0) == 0x080 and (inIterator[3] & 0x0c0) == 0x080)
        {
            uc = ((inIterator[0] & 0x007) << 18) | ((inIterator[1] & 0x03F) << 12) | ((inIterator[2] & 0x03F) << 6) | (inIterator[3] & 0x03F);
            inIterator += 4;
        }
        else
        {
            uc = 0xffef;
            ++inIterator;
        }
    }

    return make_tuple(inIterator, uc);
}

template<class OutputIterator>
inline
OutputIterator WriteUTF8(uint32 inUnicode, OutputIterator inIterator)
{
    // write out the unicode as a utf-8 string
    if (inUnicode < 0x080)
        *inIterator++ = static_cast<char>(inUnicode);
    else if (inUnicode < 0x0800)
    {
        *inIterator++ = static_cast<char> (0x0c0 | (inUnicode >> 6));
        *inIterator++ = static_cast<char> (0x080 | (inUnicode & 0x03f));
    }
    else if (inUnicode < 0x00010000)
    {
        *inIterator++ = static_cast<char> (0x0e0 | (inUnicode >> 12));
        *inIterator++ = static_cast<char> (0x080 | ((inUnicode >> 6) & 0x03f));
        *inIterator++ = static_cast<char> (0x080 | (inUnicode & 0x03f));
    }
    else
    {
        *inIterator++ = static_cast<char> (0x0f0 | (inUnicode >> 18));
        *inIterator++ = static_cast<char> (0x080 | ((inUnicode >> 12) & 0x03f));
        *inIterator++ = static_cast<char> (0x080 | ((inUnicode >> 6) & 0x03f));
        *inIterator++ = static_cast<char> (0x080 | (inUnicode & 0x03f));
    }
    return inIterator;
}

namespace uc
{

uint8 GetProperty(uint32 inUnicode)
{
    uint8 result = 0;

    if (inUnicode < 0x110000)
    {
        uint32 ix = inUnicode >> 8;
        uint32 p_ix = inUnicode & 0x00FF;

        ix = kM6UnicodeInfo.page_index[ix];
        result = kM6UnicodeInfo.data[ix][p_ix].prop;
    }

    return result;
}

uint8 GetCanonicalCombiningClass(uint32 inUnicode)
{
    uint8 result = 0;

    if (inUnicode < 0x110000)
    {
        uint32 ix = inUnicode >> 8;
        uint32 p_ix = inUnicode & 0x00FF;

        ix = kM6UnicodeInfo.page_index[ix];
        result = kM6UnicodeInfo.data[ix][p_ix].ccc;
    }

    return result;
}

bool isdigit(uint32 c)
{
    return GetProperty(c) == kNUMBER;
}

bool isalnum(uint32 c)
{
    int prop = GetProperty(c);
    return (prop == kNUMBER or prop == kLETTER)/* and
        not ((c >= 0x004e00 and c <= 0x009fff) or
             (c >= 0x003400 and c <= 0x004DFF) or
             (c >= 0x00F900 and c <= 0x00FAFF))*/;
}

bool isalpha(uint32 c)
{
    return GetProperty(c) == kLETTER;
}

bool ispunct(uint32 c)
{
    return GetProperty(c) == kPUNCTUATION;
}

bool iscombm(uint32 c)
{
    return GetProperty(c) == kCOMBININGMARK;
}

bool isprint(uint32 c)
{
    int prop = GetProperty(c);
    return (prop == kNUMBER or prop == kLETTER or prop == kPUNCTUATION);
}

bool isspace(uint32 c)
{
    return c == ' ' or c == '\r' or c == '\n' or c == '\t' or
        GetProperty(c) == kSEPARATOR;
}

bool is_han(uint32 c)
{
//    return isalnum(c) and
    int prop = GetProperty(c);
    return (prop == kNUMBER or prop == kLETTER) and
        ((c >= 0x004e00 and c <= 0x009fff) or
         (c >= 0x003400 and c <= 0x004DFF) or
         (c >= 0x00F900 and c <= 0x00FAFF));
}

bool contains_han(const string& s)
{
    bool result = false;
    string::const_iterator si = s.begin();

    while (si != s.end())
    {
        uint32 uc;
        tie(si, uc) = ReadUTF8(si);

        if (is_han(uc))
        {
            result = true;
            break;
        }
    }

    return result;
}

//bool is_katakana(uint32 c)
//{
//    return isalnum(c) and c >= 0x0030a0 and c <= 0x0030ff;
//}
//
//bool is_hiragana(uint32 c)
//{
//    return isalnum(c) and c >= 0x003040 and c <= 0x00309f;
//}
}

namespace fast
{

const uint8
    kToLowerMask     = 0x20,
    kCharIsDigitMask = 0x01,
    kCharIsAlphaMask = 0x02,
    kCharIsPunctMask = 0x04,
    kCharIsAlNumMask = kCharIsDigitMask | kCharIsAlphaMask,
    kCharPropTable[128] = {
/*        0    1    2    3    4    5    6    7    8    9    a    b    c    d    e    f    */
/* 0 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
/* 1 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
/* 2 */    0x0, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4,
/* 3 */    0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4,
/* 4 */    0x4, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2,
/* 5 */    0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x4, 0x4, 0x4, 0x4, 0x2, // '_' is alpha in my eyes
/* 6 */    0x4, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2,
/* 7 */    0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x4, 0x4, 0x4, 0x4, 0x0,
    };

inline bool isalnum(uint32 c)
{
    bool result = false;
    if (c >= 0x30)
    {
        if (c < 0x080)
            result = (kCharPropTable[c] & kCharIsAlNumMask) != 0;
        else
            result = uc::isalnum(c);
    }
    return result;
}

inline bool isalpha(uint32 c)
{
    bool result = false;
    if (c >= 0x40)
    {
        if (c < 0x080)
            result = (kCharPropTable[c] & kCharIsAlphaMask) != 0;
        else
            result = uc::isalpha(c);
    }
    return result;
}

inline bool isdigit(uint32 c)
{
    bool result = false;
    if (c >= 0x30)
    {
        if (c < 0x080)
            result = (kCharPropTable[c] & kCharIsDigitMask) != 0;
        else
            result = uc::isdigit(c);
    }
    return result;
}

inline bool ispunct(uint32 c)
{
    bool result = false;
    if (c >= 0x21)
    {
        if (c < 0x080)
            result = (kCharPropTable[c] & kCharIsPunctMask) != 0;
        else
            result = uc::ispunct(c);
    }
    return result;
}

inline bool isprint(uint32 c)
{
    bool result = false;
    if (c >= 0x21)
    {
        if (c < 0x080)
            result = (kCharPropTable[c] & (kCharIsPunctMask | kCharIsAlNumMask)) != 0;
        else
            result = uc::isprint(c);
    }
    return result;
}

inline bool is_han(uint32 c)
{
    return uc::is_han(c);
}

inline bool iscombm(uint32 c)
{
    return uc::iscombm(c);
}

inline bool isspace(uint32 c)
{
    return c == ' ' or c == '\r' or c == '\n' or c == '\t' or
        uc::isspace(c);
}

}

// --------------------------------------------------------------------

bool Decompose(uint32 inUnicode, vector<uint32>& outChars)
{
    bool result = false;
    uint32 c1 = inUnicode, c2 = 0;

    if (inUnicode < 0x110000)
    {
        uint32 ix = inUnicode >> 8;
        uint32 p_ix = inUnicode & 0x00FF;

        ix = kM6NormalisationInfo.page_index[ix];

        c1 = kM6NormalisationInfo.data[ix][p_ix][0];
        c2 = kM6NormalisationInfo.data[ix][p_ix][1];
    }

    if (c1 == 0 or c1 == inUnicode)
    {
        result = uc::GetProperty(inUnicode) == kCOMBININGMARK;
        outChars.push_back(inUnicode);
    }
    else
    {
        result = true;
        Decompose(c1, outChars);
        if (c2 != 0)
            outChars.push_back(c2);
    }

    return result;
}

inline bool ToLower(uint32 inUnicode, vector<uint32>& outChars)
{
    bool result = false;

    uint32 ix = inUnicode >> 8;
    uint32 p_ix = inUnicode & 0x00FF;

    ix = kM6UnicodeInfo.page_index[ix];
    if (kM6UnicodeInfo.data[ix][p_ix].lower == 0)
        result = Decompose(inUnicode, outChars);
    else if (kM6UnicodeInfo.data[ix][p_ix].lower != 1)
        result = Decompose(kM6UnicodeInfo.data[ix][p_ix].lower, outChars);
    else
    {
        // need a full mapping here
        int L = 0, R = sizeof(kM6FullCaseFolds) / sizeof(M6FullCaseFold) - 1;
        while (R >= L)
        {
            int i = (L + R) / 2;
            if (kM6FullCaseFolds[i].uc > inUnicode)
                R = i - 1;
            else
                L = i + 1;
        }

        assert(kM6FullCaseFolds[R].uc == inUnicode);

        for (uint32* f = kM6FullCaseFolds[R].folded; *f != 0; ++f)
        {
            if (Decompose(*f, outChars))
                result = true;
        }
    }

    return result;
}

// --------------------------------------------------------------------

M6Tokenizer::M6Tokenizer(const char* inData, size_t inLength)
    : mTokenLength(0), mLookaheadLength(0)
    , mPtr(reinterpret_cast<const uint8*>(inData)), mEnd(mPtr + inLength)
{
//    assert(inLength > 0);
}

M6Tokenizer::M6Tokenizer(const string& inData)
    : mTokenLength(0), mLookaheadLength(0)
    , mPtr(reinterpret_cast<const uint8*>(inData.c_str()))
    , mEnd(mPtr + inData.length())
{
}

uint32 M6Tokenizer::GetNextCharacter()
{
    uint32 result = 0;

    if (mLookaheadLength > 0)
    {
        --mLookaheadLength;
        result = mLookahead[mLookaheadLength];
    }
    else if (mPtr >= mEnd)
    {
        // result = 0
    }
    else if ((*mPtr & 0x080) == 0)        // next byte is a valid ASCII character
    {
        result = *mPtr++;
        if (result >= 'A' and result <= 'Z')
            result |= fast::kToLowerMask;
    }
    else    // decode utf-8
    {
        if ((mPtr[0] & 0x0E0) == 0x0C0 and (mPtr[1] & 0x0c0) == 0x080)
        {
            ToLower(((mPtr[0] & 0x01F) << 6) | (mPtr[1] & 0x03F));
            mPtr += 2;
        }
        else if ((mPtr[0] & 0x0F0) == 0x0E0 and (mPtr[1] & 0x0c0) == 0x080 and (mPtr[2] & 0x0c0) == 0x080)
        {
            ToLower(((mPtr[0] & 0x00F) << 12) | ((mPtr[1] & 0x03F) << 6) | (mPtr[2] & 0x03F));
            mPtr += 3;
        }
        else if ((mPtr[0] & 0x0F8) == 0x0F0 and (mPtr[1] & 0x0c0) == 0x080 and (mPtr[2] & 0x0c0) == 0x080 and (mPtr[3] & 0x0c0) == 0x080)
        {
            ToLower(((mPtr[0] & 0x007) << 18) | ((mPtr[1] & 0x03F) << 12) | ((mPtr[2] & 0x03F) << 6) | (mPtr[3] & 0x03F));
            mPtr += 4;
        }
        else
        {
            result = 0xffef;
            ++mPtr;
        }

        if (result == 0)
        {
            assert(mLookaheadLength > 0);
            --mLookaheadLength;
            result = mLookahead[mLookaheadLength];
        }
    }

    return result;
}

void M6Tokenizer::ToLower(uint32 inUnicode)
{
    uint32 ix = inUnicode >> 8;
    uint32 p_ix = inUnicode & 0x00FF;

    ix = kM6UnicodeInfo.page_index[ix];
    if (kM6UnicodeInfo.data[ix][p_ix].lower == 0)
        Decompose(inUnicode);
    else if (kM6UnicodeInfo.data[ix][p_ix].lower != 1)
        Decompose(kM6UnicodeInfo.data[ix][p_ix].lower);
    else
    {
        // need a full mapping here
        int L = 0, R = sizeof(kM6FullCaseFolds) / sizeof(M6FullCaseFold) - 1;
        while (R >= L)
        {
            int i = (L + R) / 2;
            if (kM6FullCaseFolds[i].uc > inUnicode)
                R = i - 1;
            else
                L = i + 1;
        }

        assert(kM6FullCaseFolds[R].uc == inUnicode);

        for (uint32* f = kM6FullCaseFolds[R].folded; *f != 0; ++f)
            Decompose(*f);
    }
}

void M6Tokenizer::Decompose(uint32 inUnicode)
{
    uint32 c1 = inUnicode, c2 = 0;

    if (inUnicode < 0x110000)
    {
        uint32 ix = inUnicode >> 8;
        uint32 p_ix = inUnicode & 0x00FF;

        ix = kM6NormalisationInfo.page_index[ix];

        c1 = kM6NormalisationInfo.data[ix][p_ix][0];
        c2 = kM6NormalisationInfo.data[ix][p_ix][1];
    }

    if (c1 == 0 or c1 == inUnicode)
    {
        if (mLookaheadLength == sizeof(mLookahead) / sizeof(uint32))
            THROW(("Lookahead buffer overflow"));

        mLookahead[mLookaheadLength] = inUnicode;
        ++mLookaheadLength;
    }
    else
    {
        if (c2 != 0)
        {
            if (mLookaheadLength == sizeof(mLookahead) / sizeof(uint32))
                THROW(("Lookahead buffer overflow"));

            mLookahead[mLookaheadLength] = c2;
            ++mLookaheadLength;
        }

        Decompose(c1);
    }
}

inline void M6Tokenizer::Retract(uint32 inUnicode)
{
    mLookahead[mLookaheadLength] = inUnicode;
    ++mLookaheadLength;
}

inline void M6Tokenizer::WriteUTF8(uint32 inString[], size_t inLength)
{
    char* t = mTokenText;
    for (uint32 i = 0; i < inLength; ++i)
    {
        if (t + 6 > mTokenText + kTokenBufferLength)
            THROW(("Token buffer overflow"));

        t = ::WriteUTF8(inString[i], t);
    }

    mTokenLength = static_cast<uint32>(t - mTokenText);
}

M6Token M6Tokenizer::GetNextWord()
{
    M6Token result = eM6TokenNone;

    mTokenLength = 0;
    uint32 token[kMaxTokenLength + 1];
    uint32* t = token;
    bool hasCombiningMarks = false;

    int state = 10;
    while (result == eM6TokenNone)
    {
        uint32 c = GetNextCharacter();

        // this tokens exceeds the max token length... to not overflow the buffer and return
        // a undefined token.
        if (t >= token + kMaxTokenLength)
        {
            result = eM6TokenUndefined;

            while (not (c == 0 or fast::ispunct(c) or fast::isspace(c)))
                c = GetNextCharacter();

            break;
        }

        *t++ = c;

        switch (state)
        {
            case 10:
                if (c == 0)
                    result = eM6TokenEOF;
                else if (fast::isspace(c))
                    t = token;
                else if (fast::is_han(c))        // chinese
                    result = eM6TokenWord;
                else if (fast::isdigit(c))        // first try a number
                    state = 20;
                else if (fast::isalnum(c))
                    state = 30;
                else if (uc::iscombm(c))
                {
                    hasCombiningMarks = true;
                    state = 30;
                }
                else if (fast::ispunct(c))
                    result = eM6TokenPunctuation;
                else
                    state = 40;
                break;

            // matched a digit, allow only integers or an identifier starting with a digit
            case 20:
                if (fast::isalpha(c))
                    state = 30;
                else if (uc::iscombm(c))
                {
                    hasCombiningMarks = true;
                    state = 30;
                }
                else if (not fast::isdigit(c) and c != '-')
                {
                    Retract(*--t);
                    result = eM6TokenNumber;
                }
                break;

            // parse identifiers
            case 30:
                if (fast::iscombm(c))
                    hasCombiningMarks = true;
                else if (fast::is_han(c) or not fast::isalnum(c))
                {
                    Retract(*--t);
                    result = eM6TokenWord;
                }
                break;

            // anything else, eat as much as we can
            case 40:
                if (c == 0 or fast::isprint(c) or fast::isspace(c))
                {
                    Retract(*--t);
                    result = eM6TokenUndefined;
                }
                break;

            default:
                THROW(("Inconsisten tokenizer state"));
        }
    }

    *t = 0;

    if (hasCombiningMarks)
        Reorder(token, t - token);

    WriteUTF8(token, t - token);

    return result;
}

M6Token M6Tokenizer::GetNextQueryToken()
{
    M6Token result = eM6TokenNone;

    const uint8* b = mPtr;

    mTokenLength = 0;
    uint32 token[kMaxTokenLength];
    uint32* t = token;
    bool hasCombiningMarks = false, isPattern = false;
    uint32 quote = 0;    // keep compiler silent

    int state = 10;

    auto Restart = [&t,&token,&state,this](int s)
    {
        while (t > token)
            this->Retract(*--t);
        state = s;
    };

    while (result == eM6TokenNone)
    {
        // this tokens exceeds the max token length... to not overflow the buffer and return
        // a undefined token.
        if (t >= token + kMaxTokenLength)
        {
            result = eM6TokenUndefined;
            break;
        }

        uint32 c = GetNextCharacter();
        *t++ = c;

        switch (state)
        {
            case 10:
                switch (c)
                {
                    case 0:        result = eM6TokenEOF; break;
//                    case '-':    result = eM6TokenHyphen; break;
//                    case '+':    result = eM6TokenPlus; break;
                    case '(':    result = eM6TokenOpenParenthesis; break;
                    case ')':    result = eM6TokenCloseParenthesis; break;
                    case '[':    result = eM6TokenOpenBracket; break;
                    case ']':    result = eM6TokenCloseBracket; break;
                    case '/':    result = eM6TokenSlash; break;
                    case ':':    result = eM6TokenColon; break;
                    case '=':    result = eM6TokenEquals; break;
                    case '<':    state = 11; break;
                    case '>':    state = 12; break;
                    case '\'':
                    case '"':    quote = c; state = 40; break;
                    case '?':
                    case '*':    isPattern = true; state = 30; break;
                    case '|':    result = eM6TokenOR; break;
                    case '&':    result = eM6TokenAND; break;
                    case '#':    state = 50; break;
                    case '-':    state = 201; break;
                    case '+':    state = 202; break;
                    default:
                        if (fast::isspace(c))
                        {
                            t = token;
                            b = mPtr;
                        }
                        else if (fast::is_han(c))        // chinese
                            result = eM6TokenWord;
                        else if (fast::isdigit(c))
                            state = 203;
                        else if (fast::isalnum(c) or c == '.')
                            state = 30;
                        else if (fast::ispunct(c) or c == '-' or c == '+')
                            result = eM6TokenPunctuation;
                        else
                            state = 60;
                        break;
                }
                break;

            case 11:    // match <
                if (c == '=')
                    result = eM6TokenLessEqual;
                else
                {
                    Retract(*--t);
                    result = eM6TokenLessThan;
                }
                break;

            case 12:    // match >
                if (c == '=')
                    result = eM6TokenGreaterEqual;
                else
                {
                    Retract(*--t);
                    result = eM6TokenGreaterThan;
                }
                break;

            // test for numbers since we found '-' or '+'
            case 201: // '-' seen
                if (c >= '0' and c <= '9')
                    state = 203;
                else
                {
                    Retract(*--t);
                    result = eM6TokenPunctuation;
                }
                break;

            case 202: // '+' seen
                if (c >= '0' and c <= '9')
                    state = 203;
                else
                {
                    Retract(*--t);
                    result = eM6TokenPunctuation;
                }
                break;

            case 203: // previous char was digit
                if (c == '.')
                    state = 204;
                else if (c == 'e' or c == 'E')
                    state = 205;
                else
                    Restart(20);
                break;

            case 204:
                if (c == 'e' or c == 'E')
                    state = 205;
                else if (c < '0' or c > '9')
                {
                    Retract(*--t);
                    result = eM6TokenFloat;
                }
                break;

            case 205: // previous was float pattern: [0-9][Ee], -/+ is optional
                if (c == '+' or c == '-')

                    state = 206;

                // otherwise fall through

            case 206: // previous was float pattern: [0-9][Ee][-+]?
                if (c >= '0' and c <= '9')

                    state = 207;
                else
                    /* Discard the entire pattern, it can't be a [0-9][Ee][-+]?[0-9] float.
                       It might be a [0-9] number though.
                     */
                    Restart(20);
                break;

            case 207: // previous was float pattern: [0-9][Ee][-+]?[0-9]
                if (c < '0' or c > '9')
                {
                    // No more digits to add to this number
                    Retract(*--t);
                    result = eM6TokenFloat;
                }
                break;

            // matched a digit, allow only integers or an identifier starting with a digit
            case 20:
                if (fast::isalpha(c))
                    state = 30;
                else if (c == '?' or c == '*')
                {
                    isPattern = true;
                    state = 30;
                }
                else if (not fast::isdigit(c) and c != '-')
                {
                    Retract(*--t);
                    result = eM6TokenNumber;
                }
                break;

            case 30:
            // parse identifiers
                if (fast::iscombm(c))
                    hasCombiningMarks = true;
                else if (c == '?' or c == '*')
                    isPattern = true;
                else if (fast::is_han(c) or not (fast::isalnum(c) or c == '.' or c == '-'))
                {
                    Retract(*--t);
                    result = isPattern ? eM6TokenPattern : eM6TokenWord;
                }
                break;

            // quoted strings
            case 40:
                if (c == quote)
                    result = eM6TokenString;
                else if (c == '\\')
                {
                    --t;
                    state = 41;
                }
                else if (c == 0)
                    throw M6TokenUnterminatedStringException();
                break;

            case 41:
                state = 40;
                break;

            // document numbers?
            case 50:
                if (uc::isdigit(c))
                    state = 51;
                else
                {
                    Retract(*--t);
                    result = eM6TokenUndefined;
                }
                break;

            case 51:
                if (not uc::isdigit(c))
                {
                    Retract(*--t);
                    result = eM6TokenDocNr;
                }
                break;

            // anything else, eat as much as we can
            case 60:
                if (c == 0 or fast::isprint(c) or fast::isspace(c))
                {
                    Retract(*--t);
                    result = eM6TokenUndefined;
                }
                break;

            default:
                THROW(("Inconsisten tokenizer state"));
        }
    }

    *t = 0;

    if (hasCombiningMarks)
        Reorder(token, t - token);

    if (result == eM6TokenString)
        WriteUTF8(token + 1, t - token - 2);
    else if (result == eM6TokenDocNr)
        WriteUTF8(token + 1, t - token - 1);
    else
        WriteUTF8(token, t - token);

    if (result == eM6TokenWord)
    {
        if (mTokenLength == 2 and strncmp(reinterpret_cast<const char*>(b), "OR", 2) == 0)
            result = eM6TokenOR;
        else if (mTokenLength == 3 and strncmp(reinterpret_cast<const char*>(b), "AND", 3) == 0)
            result = eM6TokenAND;
        else if (mTokenLength == 3 and strncmp(reinterpret_cast<const char*>(b), "NOT", 3) == 0)
            result = eM6TokenNOT;
        else if (mTokenLength == 7 and strncmp(reinterpret_cast<const char*>(b), "BETWEEN", 7) == 0)
            result = eM6TokenBETWEEN;
    }

    return result;
}

void M6Tokenizer::CaseFold(string& ioString)
{
    vector<uint32> s;
    s.reserve(ioString.length());

    const char* ptr = ioString.c_str();
    const char* end = ptr + ioString.length();
    bool hasCombiningMarks = false, hasUpperCase = false;

    while (ptr < end)
    {
        uint32 ch = 0;
        tie(ptr, ch) = ReadUTF8(ptr);
        if (::ToLower(ch, s))
            hasCombiningMarks = true;
        if (s.size() > 1 or (s.size() == 1 and s[0] != ch))
            hasUpperCase = true;
    }

    if (hasCombiningMarks)
        Reorder(&s[0], s.size());

    if (hasCombiningMarks or hasUpperCase)
    {
        ioString.clear();
        for (uint32 ch : s)
            ::WriteUTF8(ch, back_inserter(ioString));
    }
}

void M6Tokenizer::Normalize(string& ioString)
{
    vector<uint32> s;
    s.reserve(ioString.length());

    const char* ptr = ioString.c_str();
    const char* end = ptr + ioString.length();
    bool hasCombiningMarks = false;

    while (ptr < end)
    {
        uint32 ch = 0;
        tie(ptr, ch) = ReadUTF8(ptr);
        if (::Decompose(ch, s))
            hasCombiningMarks = true;
    }

    if (hasCombiningMarks)
    {
        Reorder(&s[0], s.size());

        ioString.clear();
        for (uint32 ch : s)
            ::WriteUTF8(ch, back_inserter(ioString));
    }
}

void M6Tokenizer::Reorder(uint32 inString[], size_t inLength)
{
    // recorder combining marks
    uint32* s = inString;
    uint32* t = inString + inLength;

    auto order = [](uint32 a, uint32 b) -> bool
    {
        return uc::GetCanonicalCombiningClass(a) < uc::GetCanonicalCombiningClass(b);
    };

    while (s + 1 < t)
    {
        if (uc::GetProperty(*s) != kCOMBININGMARK)
        {
            ++s;
            continue;
        }

        uint32* s2 = s + 1;
        while (uc::GetProperty(*s2) == kCOMBININGMARK)
            ++s2;

        if (s2 > s + 1)
            sort(s, s2, order);

        s = s2;
    }
}

