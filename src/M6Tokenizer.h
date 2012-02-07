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
	eM6TokenNumber,
//	eM6TokenCardinal,
	eM6TokenPunctuation,
	eM6TokenOther
};

class M6Tokenizer
{
  public:

	static const uint32 kMaxTokenLength = 255;

					M6Tokenizer(const char* inData);
					M6Tokenizer(const char* inData, size_t inLength,
						bool inCaseInsensitive = true);

	M6Token			GetToken();

	const char*		GetTokenValue() const			{ return mTokenText; }
	uint32			GetTokenLength() const			{ return static_cast<uint32>(mToken - mTokenText); }
	std::string		GetTokenString() const			{ return std::string(GetTokenValue(), GetTokenLength()); }
	
	bool			TokenHasUpperCase() const		{ return mHasUpperCase; }
	
	uint32			GetOffset() const				{ return static_cast<uint32>(mPtr - mBuffer); }
	void			SetOffset(uint32 inOffset);
	
  private:
	
	uint32			GetNextCharacter();
	void			Retract();
	int				Restart(int inStart);

	char			mTokenText[kMaxTokenLength + 5];		// private buffer
	const uint8*	mBuffer;
	size_t			mBufferSize;
	const uint8*	mPtr;
	char*			mToken;
	bool			mCaseSensitive, mHasUpperCase;
};
