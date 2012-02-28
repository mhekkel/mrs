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

// --------------------------------------------------------------------
// some text routines first

void CaseFold(std::string& ioString);

// --------------------------------------------------------------------

enum M6Token
{
	eM6TokenNone,
	eM6TokenEOF,
	eM6TokenWord,
	eM6TokenCardinal,
	eM6TokenNumber = eM6TokenCardinal,

	eM6TokenHyphen,
	eM6TokenPlus,
	eM6TokenQuestionMark,
	eM6TokenAsterisk,
	eM6TokenPipe,
	eM6TokenAmpersand,
	eM6TokenSingleQuote,
	eM6TokenDoubleQuote,
	eM6TokenPeriod,
	eM6TokenOpenParenthesis,
	eM6TokenCloseParenthesis,
	eM6TokenColon,
	eM6TokenEquals,
	eM6TokenLessThan,
	eM6TokenLessEqual,
	eM6TokenGreaterEqual,
	eM6TokenGreaterThan,
	
	eM6TokenPunctuation,
	eM6TokenOther
};

class M6Tokenizer
{
  public:

	static const uint32 kMaxTokenLength = 255;

					M6Tokenizer(const std::string& inData);
					M6Tokenizer(const char* inData, size_t inLength);

	M6Token			GetNextToken();

	const char*		GetTokenValue() const			{ return mTokenText; }
	size_t			GetTokenLength() const			{ return mTokenLength; }
	std::string		GetTokenString() const			{ return std::string(GetTokenValue(), GetTokenLength()); }
	
  private:
	
	uint32			GetNextCharacter();
	void			Retract(uint32 inUnicode);
	
	void			ToLower(uint32 inUnicode);
	void			Decompose(uint32 inUnicode);

	char			mTokenText[kMaxTokenLength];	// private buffer
	uint32			mTokenLength;
	
	uint32			mLookahead[10];
	uint32			mLookaheadLength;

	const uint8*	mPtr;
	const uint8*	mEnd;

					M6Tokenizer(const M6Tokenizer&);
	M6Tokenizer&	operator=(const M6Tokenizer&);
};

