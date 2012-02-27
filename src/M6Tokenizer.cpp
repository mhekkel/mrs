#include "M6Lib.h"

#include <cassert>
#include <cstring>

#include "M6Tokenizer.h"
//#include "M6Unicode.h"
#include "../unicode/M6UnicodeTables.h"
#include "M6Error.h"

using namespace std;

namespace uc
{

// to lower does case folding
uint32* ToLower(uint32 inUnicode, uint32* outLower)
{
	if (inUnicode < 0x110000)
	{
		uint32 ix = inUnicode >> 8;
		uint32 p_ix = inUnicode & 0x00FF;
		
		ix = kM6UnicodeInfo.page_index[ix];
		if (kM6UnicodeInfo.data[ix][p_ix].lower == 0)
			*outLower++ = inUnicode;
		else if (kM6UnicodeInfo.data[ix][p_ix].lower != 1)
			*outLower++ = kM6UnicodeInfo.data[ix][p_ix].lower;
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
			
			for (uint32* f = kM6FullCaseFolds[R + 1].folded; *f != 0; ++f)
				*outLower++ = *f;
		}
	}
	
	return outLower;
}

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

bool isspace(uint32 c)
{
	return c == ' ' or c == '\r' or c == '\n' or c == '\t' or
		GetProperty(c) == kSEPARATOR;
}

uint32 tolower(uint32 c)
{
	const uint8 kToLowerMask = 0x20;
	uint32 result;
	
	if (c < 128)
	{
		if (c >= 'A' and c <= 'Z')
			c |= kToLowerMask;
		result = c;
	}
	else
		assert(false);
		//result = ToLower(c);
	
	return result;
}

//UCProperty property(uint32 c)
//{
//	return UCProperty(GetProperty(c));
//}

bool is_han(uint32 c)
{
//	return isalnum(c) and
	int prop = GetProperty(c);
	return (prop == kNUMBER or prop == kLETTER) and
		((c >= 0x004e00 and c <= 0x009fff) or
		 (c >= 0x003400 and c <= 0x004DFF) or
		 (c >= 0x00F900 and c <= 0x00FAFF));
}

//bool contains_han(const string& s)
//{
//	bool result = false;
//	string::const_iterator si = s.begin();
//	
//	while (si != s.end())
//	{
//		uint32 uc;
//		uint32 length;
//		
//		tie(uc, length) = ReadUnicode(si);
//		
//		if (is_han(uc))
//		{
//			result = true;
//			break;
//		}
//		
//		si += length;
//	}
//	
//	return result;
//}
//
//bool is_katakana(uint32 c)
//{
//	return isalnum(c) and c >= 0x0030a0 and c <= 0x0030ff;
//}
//
//bool is_hiragana(uint32 c)
//{
//	return isalnum(c) and c >= 0x003040 and c <= 0x00309f;
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
/*		0    1    2    3    4    5    6    7    8    9    a    b    c    d    e    f	*/
/* 0 */	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
/* 1 */	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
/* 2 */	0x0, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 
/* 3 */	0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 
/* 4 */	0x4, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 
/* 5 */	0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x4, 0x4, 0x4, 0x4, 0x4, 
/* 6 */	0x4, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 
/* 7 */	0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x4, 0x4, 0x4, 0x4, 0x0, 
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

M6Tokenizer::M6Tokenizer(const char* inData, size_t inLength)
	: mTokenLength(0), mLookaheadLength(0)
	, mPtr(reinterpret_cast<const uint8*>(inData)), mEnd(reinterpret_cast<const uint8*>(inData) + inLength)
{
	assert(inLength > 0);
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
		
	}
	else if ((*mPtr & 0x080) == 0)		// next byte is a valid ASCII character
	{
		result = *mPtr++;
		if (result >= 'A' and result <= 'Z')
			result |= fast::kToLowerMask;
	}
	else	// decode utf-8
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
		
		ix = kNormalisationInfo.page_index[ix];
		
		c1 = kNormalisationInfo.data[ix][p_ix][0];
		c2 = kNormalisationInfo.data[ix][p_ix][1];
	}
	
	if (c1 == 0 or c1 == inUnicode)
	{
		mLookahead[mLookaheadLength] = inUnicode;
		++mLookaheadLength;
	}
	else
	{
		assert(c2 != 0);
		
		mLookahead[mLookaheadLength] = c2;
		++mLookaheadLength;
		Decompose(c1);
	}
}

inline void M6Tokenizer::Retract(uint32 inUnicode)
{
	mLookahead[mLookaheadLength] = inUnicode;
	++mLookaheadLength;
}

M6Token M6Tokenizer::GetNextToken()
{
	M6Token result = eM6TokenNone;
	int start = 10, state = start;

	mTokenLength = 0;
	uint32 token[kMaxTokenLength];
	uint32* t = token;
		
	while (result == eM6TokenNone and mTokenLength < kMaxTokenLength)
	{
		uint32 c = GetNextCharacter();
		*t++ = c;
		
		switch (state)
		{
			case 10:
				switch (c)
				{
					case 0:		result = eM6TokenEOF; break;
					case '-':	result = eM6TokenHyphen; break;
					case '+':	result = eM6TokenPlus; break;
					case '?':	result = eM6TokenQuestionMark; break;
					case '*':	result = eM6TokenAsterisk; break;
					case '\'':	result = eM6TokenSingleQuote; break;
					case '"':	result = eM6TokenDoubleQuote; break;
					case '.':	result = eM6TokenPeriod; break;
					case '(':	result = eM6TokenOpenParenthesis; break;
					case ')':	result = eM6TokenCloseParenthesis; break;
					case ':':	result = eM6TokenColon; break;
					case '=':	result = eM6TokenEquals; break;
					case '<':	state = 11; break;
					case '>':	state = 12; break;
					default:
						if (fast::isspace(c))
							t = token;
						else if (fast::is_han(c))		// chinese
							result = eM6TokenWord;
						else if (fast::isdigit(c))		// first try a number
							state = 20;
						else if (fast::isalnum(c) or c == '_')
							state = 30;
						else if (fast::ispunct(c))
							result = eM6TokenPunctuation;
						else
							state = 40;
						break;
				}
				break;
			
			case 11:	// match <
				if (c == '=')
					result = eM6TokenLessEqual;
				else
				{
					Retract(*--t);
					result = eM6TokenLessThan;
				}
				break;
		
			case 12:	// match >
				if (c == '=')
					result = eM6TokenGreaterEqual;
				else
				{
					Retract(*--t);
					result = eM6TokenGreaterThan;
				}
				break;
		
			// matched a digit, allow only cardinals or an identifier starting with a digit
			case 20:				
				if (fast::isalpha(c) or c == '_')	
					state = 30;
				else if (not fast::isdigit(c))
				{
					Retract(*--t);
					result = eM6TokenCardinal;
				}
				break;
		
			case 30:
			// parse identifiers
				if (fast::is_han(c) or not (fast::isalnum(c) or c == '_' or fast::iscombm(c)))
				{
					Retract(*--t);
					result = eM6TokenWord;
				}
				break;
			
			// anything else, eat as much as we can
			case 40:
				if (fast::isalnum(c) or c == '_' or c == 0 or fast::ispunct(c))
				{
					Retract(*--t);
					result = eM6TokenOther;
				}
				break;
			
			default:
				THROW(("Inconsisten tokenizer state"));
		}
	}
	
	*t = 0;
	
	// recorder combining marks
	uint32* s = token;
	while (s != t)
	{
		if (uc::GetProperty(*s) != kCOMBININGMARK)
		{
			++s;
			continue;
		}
		
		uint8 cc[10];
		cc[0] = uc::GetCanonicalCombiningClass(*s);
		
		uint32 n = 1;
		
		uint32* ss = s + 1;
		while (ss != t)
		{
			if (uc::GetProperty(*ss) == kCOMBININGMARK)
			{
				cc[n] = uc::GetCanonicalCombiningClass(*ss);
				++ss;
				++n;
				if (n < 10)
					continue;
			}
			break;
		}
		
		for (uint32 i = 0; i + 1 < n; ++i)
		{
			for (uint32 j = i + 1; j < n; ++j)
			{
				if (cc[i] > cc[j])
				{
					swap(cc[i], cc[j]);
					swap(s[i], s[j]);
				}
			}
		}

		s = ss;
	}
	
	char* tp = mTokenText;
	for (s = token; s != t; ++s)
	{
		uint32 uc = *s;
		
		// write the unicode to our token mBuffer
		if (uc < 0x080)
			*tp++ = static_cast<char>(uc);
		else if (uc < 0x0800)
		{
			*tp++ = static_cast<char> (0x0c0 | (uc >> 6));
			*tp++ = static_cast<char> (0x080 | (uc & 0x03f));
		}
		else if (uc < 0x00010000)
		{
			*tp++ = static_cast<char> (0x0e0 | (uc >> 12));
			*tp++ = static_cast<char> (0x080 | ((uc >> 6) & 0x03f));
			*tp++ = static_cast<char> (0x080 | (uc & 0x03f));
		}
		else
		{
			*tp++ = static_cast<char> (0x0f0 | (uc >> 18));
			*tp++ = static_cast<char> (0x080 | ((uc >> 12) & 0x03f));
			*tp++ = static_cast<char> (0x080 | ((uc >> 6) & 0x03f));
			*tp++ = static_cast<char> (0x080 | (uc & 0x03f));
		}
	}
	
	mTokenLength = static_cast<uint32>(tp - mTokenText);

	return result;
}
