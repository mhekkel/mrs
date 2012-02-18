#include "M6Lib.h"

#include <cassert>
#include <cstring>

#include "M6Tokenizer.h"
//#include "M6Unicode.h"
#include "M6UnicodeTables.h"
#include "M6Error.h"

using namespace std;

namespace uc
{

uint32 ToLower(uint32 inUnicode)
{
	uint32 result = inUnicode;
	
	if (inUnicode < 0x110000)
	{
		uint32 ix = inUnicode >> 8;
		uint32 p_ix = inUnicode & 0x00FF;
		
		ix = kUnicodeInfo.page_index[ix];
		if (kUnicodeInfo.data[ix][p_ix].lower != 0)
			result = kUnicodeInfo.data[ix][p_ix].lower;
	}
	
	return result;
}

uint8 GetProperty(uint32 inUnicode)
{
	uint8 result = 0;
	
	if (inUnicode < 0x110000)
	{
		uint32 ix = inUnicode >> 8;
		uint32 p_ix = inUnicode & 0x00FF;
		
		ix = kUnicodeInfo.page_index[ix];
		result = kUnicodeInfo.data[ix][p_ix].prop;
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
		result = ToLower(c);
	
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

M6Tokenizer::M6Tokenizer(const char* inData)
	: mBuffer(reinterpret_cast<const uint8*>(inData))
	, mBufferSize(strlen(inData))
	, mPtr(mBuffer)
	, mCaseSensitive(true)
{
}

M6Tokenizer::M6Tokenizer(const char* inData, size_t inLength, bool inCaseInsensitive)
	: mBuffer(reinterpret_cast<const uint8*>(inData))
	, mBufferSize(inLength)
	, mPtr(mBuffer)
	, mCaseSensitive(inCaseInsensitive)
{
	assert(inLength > 0);
}

uint32 M6Tokenizer::GetNextCharacter()
{
	uint32 result = 0;

	if (mPtr >= mBuffer + mBufferSize)
	{
		++mPtr;
		++mToken;
		return result;
	}
	
	// if it is a simple ascii character:
	if ((mPtr[0] & 0x080) == 0)
	{
		result = *mPtr++;
		if (mCaseSensitive and result >= 'A' and result <= 'Z')
			result |= fast::kToLowerMask;
		*mToken++ = static_cast<char>(result);
	}
	else
	{
		if ((mPtr[0] & 0x0E0) == 0x0C0 and (mPtr[1] & 0x0c0) == 0x080)
		{
			result = ((mPtr[0] & 0x01F) << 6) | (mPtr[1] & 0x03F);
			mPtr += 2;
		}
		else if ((mPtr[0] & 0x0F0) == 0x0E0 and (mPtr[1] & 0x0c0) == 0x080 and (mPtr[2] & 0x0c0) == 0x080)
		{
			result = ((mPtr[0] & 0x00F) << 12) | ((mPtr[1] & 0x03F) << 6) | (mPtr[2] & 0x03F);
			mPtr += 3;
		}
		else if ((mPtr[0] & 0x0F8) == 0x0F0 and (mPtr[1] & 0x0c0) == 0x080 and (mPtr[2] & 0x0c0) == 0x080 and (mPtr[3] & 0x0c0) == 0x080)
		{
			result = ((mPtr[0] & 0x007) << 18) | ((mPtr[1] & 0x03F) << 12) | ((mPtr[2] & 0x03F) << 6) | (mPtr[3] & 0x03F);
			mPtr += 4;
		}
		else
		{
			result = 0xffef;
			++mPtr;
		}
	
		if (not mCaseSensitive)
			result = uc::tolower(result);

		// write the unicode to our token mBuffer
		if (result < 0x080)
			*mToken++ = static_cast<char>(result);
		else if (result < 0x0800)
		{
			*mToken++ = static_cast<char> (0x0c0 | (result >> 6));
			*mToken++ = static_cast<char> (0x080 | (result & 0x03f));
		}
		else if (result < 0x00010000)
		{
			*mToken++ = static_cast<char> (0x0e0 | (result >> 12));
			*mToken++ = static_cast<char> (0x080 | ((result >> 6) & 0x03f));
			*mToken++ = static_cast<char> (0x080 | (result & 0x03f));
		}
		else
		{
			*mToken++ = static_cast<char> (0x0f0 | (result >> 18));
			*mToken++ = static_cast<char> (0x080 | ((result >> 12) & 0x03f));
			*mToken++ = static_cast<char> (0x080 | ((result >> 6) & 0x03f));
			*mToken++ = static_cast<char> (0x080 | (result & 0x03f));
		}
	}
	
	return result;
}

inline void M6Tokenizer::Retract()
{
	// skip one valid character back in the input mBuffer
	do --mToken; while ((*mToken & 0x0c0) == 0x080 and mToken > mTokenText);
	do --mPtr; while ((*mPtr & 0x0c0) == 0x080 and mPtr > mBuffer);
}

int M6Tokenizer::Restart(int inStart)
{
	while (mToken > mTokenText)
		Retract();
	
	int result;
	switch (inStart)
	{
		case 10: result = 20; break;
		case 20: result = 30; break;
	}

	return result;
}

M6Token M6Tokenizer::GetToken()
{
	M6Token result = eM6TokenNone;
	int start = 10, state = start;

	mToken = mTokenText;
	
	while (result == eM6TokenNone and mToken < mTokenText + kMaxTokenLength)
	{
		uint32 c = GetNextCharacter();
		
		switch (state)
		{
			case 10:
				if (c == 0)	// done!
					result = eM6TokenEOF;
				else if (fast::isspace(c))
					mToken = mTokenText;
				else if (fast::isdigit(c))		// first try a number
					state = 11;
				else if (fast::is_han(c))
					result = eM6TokenWord;
				else if (fast::isalnum(c) or c == '_')
					state = 21;
				else if (fast::ispunct(c))
					result = eM6TokenPunctuation;
				else
					state = 30;
				break;
			
			case 11:
				if (c == '.')		// scientific notation perhaps?
					state = 12;
				else if (c == 'e' or c == 'E')
					state = 14;
				else if (fast::isalpha(c) or c == '_' or c == '-')		// [:digit:]+[-[:alpha:]_]	=> ident
					state = start = Restart(start);
				else if (not fast::isdigit(c))
				{
					Retract();
					result = eM6TokenNumber;
				}
				break;
			
			case 12:
				if (c == 'e' or c == 'E')
					state = 14;
				else if (fast::isalpha(c) or c == '_' or c == '-')		// [:digit:]+.[:digit:]+[-[:alpha:]_]	=> ident
					state = start = Restart(start);
				else if (not fast::isdigit(c))
				{
					Retract();
					result = eM6TokenNumber;
				}
				break;
			
			case 14:
				if (c == '+' or c == '-' or fast::isdigit(c))
					state = 15;
				else
					state = start = Restart(start);
				break;
			
			case 15:
				if (fast::isalpha(c))
					state = start = Restart(start);
				else if (not fast::isdigit(c))
				{
					Retract();
					result = eM6TokenNumber;
				}
				break;
			
			// parse identifiers
			case 20:
				if (fast::isalnum(c) or c == '_')
					state = 21;
				else
					state = start = Restart(start);
				break;
			
			case 21:
				if (fast::is_han(c))
				{
					Retract();
					result = eM6TokenWord;
				}
				else if (c == '.' or c == '-' or c == '\'')
					state = 22;
				else if (not (fast::isalnum(c) or c == '_' or fast::iscombm(c)))
				{
					Retract();
					result = eM6TokenWord;
				}
				break;

			// no trailing or double dots, hyphens and single quotes
			case 22:
				if (fast::isalnum(c) and not fast::is_han(c))
						// watch out, we only accept one period in between alnum's
					state = 21;
				else
				{
					Retract();
					Retract();
					result = eM6TokenWord;
				}
				break;
			
			// anything else, eat as much as we can
			case 30:
				if (fast::isalnum(c) or c == '_' or c == 0 or fast::ispunct(c))
				{
					Retract();
					result = eM6TokenOther;
				}
				break;
			
			default:
				THROW(("Inconsisten tokenizer state"));
		}
	}
	
	// can happen if token is too long:
	if (result == eM6TokenNone and mToken > mTokenText)
		result = eM6TokenOther;

	if (result == eM6TokenWord and mCaseSensitive)
	{
		int32 lowerCase = 0, upperCase = 0;
		
		for (char* mPtr = mTokenText; mPtr != mToken; ++mPtr)
		{
			if (*mPtr >= 'a' and *mPtr <= 'z')
				++lowerCase;
			else if (*mPtr >= 'A' and *mPtr <= 'Z')
				++upperCase;
		}
		
		if (upperCase < lowerCase)
		{
			// only convert latin uppercase letters to lowercase
			for (char* mPtr = mTokenText; mPtr != mToken; ++mPtr)
			{
				if (*mPtr >= 'A' and *mPtr <= 'Z')
					*mPtr |= fast::kToLowerMask;
			}
		}
	}

	*mToken = 0;

	return result;
}

void M6Tokenizer::SetOffset(uint32 inOffset)
{
	if (inOffset > mBufferSize)
		THROW(("Parameter for M6Tokenizer::SetOffset is out of range"));

	mPtr = mBuffer + inOffset;
}
