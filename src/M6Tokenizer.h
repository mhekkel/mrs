#pragma once

/*
	M6Tokenizer is a class that can split a string of characters into tokens.
	It assumes input is in Unicode (in UTF-8 encoding).
	
	The recognised tokens are: words, numbers, punctuation and other.
	
	The recognized numbers are integers and floating point numbers in
	scientific notation. In regular expression this would be:
	
	[0-9]+(\.[0-9]*)?([eE](+|-)?[0-9]+)?
	
	Words are sequences of numbers, letters (optionally followed by 
	combining marks), hyphens or periods. hyphens and dots should have
	neighbouring letters or numbers. Trailing dots and numbers are also
	not valid. So, in (incomplete) regular expression:
	
	[0-9a-z_]+([-.][0-9a-z_]+)*

	Punctuation are characters that have the punctuation property.
	
	As an extra, chinese Han characters are recognized as separate words.
	
	M6Tokenizer does two other things. If the inCaseInsensitive flag is
	set, all tokens are converted to lower-case. If the flag is not set,
	conversion is done only done if the number of upper case characters
	is less than the number of lower case ones. This is to avoid
	converting gene names to lower case.
*/

#include <string>

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

//					M6Tokenizer(const char* inData);
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

	char			mTokenText[kMaxTokenLength];		// private buffer
	uint32			mTokenLength;
	
	uint32			mLookahead[10];
	uint32			mLookaheadLength;

	const uint8*	mPtr;
	const uint8*	mEnd;
};
