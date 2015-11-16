//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

/*
	M6Tokenizer is a class that can split a string of characters into tokens.
	It assumes input is in Unicode (in UTF-8 encoding).
	
	Only some basic tokens are recognized, words, numbers (cardinals) and
	tokens that are used in query strings as operators.
	
	As an extra, chinese Han characters are recognized as separate words.
	
	Apart from this, the output tokens are in lower case and denormalized.
	(NFD)
*/

#include <string>
#include <exception>

// --------------------------------------------------------------------

enum M6Token
{
	eM6TokenNone,
	eM6TokenEOF,
	eM6TokenUndefined,		// any garbage not matched
	eM6TokenWord,
	eM6TokenNumber,
	eM6TokenPunctuation,	// only returned by GetNextWord

	// tokens returned by GetNextQueryToken:

	eM6TokenString,			// a quoted string
	eM6TokenFloat,			// a double precision floating point number
	eM6TokenPattern,		// a glob-like pattern
	eM6TokenDocNr,			// #1234 an explicit document number
//	eM6TokenHyphen,
//	eM6TokenPlus,
	eM6TokenOR,
	eM6TokenAND,
	eM6TokenNOT,
	eM6TokenBETWEEN,
	eM6TokenOpenParenthesis,
	eM6TokenCloseParenthesis,
	eM6TokenOpenBracket,
	eM6TokenCloseBracket,
	eM6TokenSlash,
	eM6TokenColon,
	eM6TokenEquals,
	eM6TokenLessThan,
	eM6TokenLessEqual,
	eM6TokenGreaterEqual,
	eM6TokenGreaterThan
};

std::ostream& operator<<(std::ostream& os, M6Token token);

class M6TokenUnterminatedStringException : public std::exception
{
	const char*		what() const throw() { return "Unterminated string"; }
};

class M6Tokenizer
{
  public:

	static const uint32 kMaxTokenLength = 255, kTokenBufferLength = 4 * kMaxTokenLength + 32;

					M6Tokenizer(const std::string& inData);
					M6Tokenizer(const char* inData, size_t inLength);

	M6Token			GetNextWord();
	M6Token			GetNextQueryToken();

	const char*		GetTokenValue() const			{ return mTokenText; }
	size_t			GetTokenLength() const			{ return mTokenLength; }
	std::string		GetTokenString() const			{ return std::string(GetTokenValue(), GetTokenLength()); }

	// code reuse
	static void		CaseFold(std::string& ioString);
	static void		Normalize(std::string& ioString);
	
  private:
	
	uint32			GetNextCharacter();
	void			Retract(uint32 inUnicode);
	
	void			ToLower(uint32 inUnicode);
	void			Decompose(uint32 inUnicode);

	static void		Reorder(uint32 inString[], size_t inLength);
	void			WriteUTF8(uint32 inString[], size_t inLength);

	char			mTokenText[kTokenBufferLength];	// private buffer
	uint32			mTokenLength;
	
	uint32			mLookahead[32];
	uint32			mLookaheadLength;

	const uint8*	mPtr;
	const uint8*	mEnd;

					M6Tokenizer(const M6Tokenizer&);
	M6Tokenizer&	operator=(const M6Tokenizer&);
};

namespace uc
{
bool contains_han(const std::string& s);
}
