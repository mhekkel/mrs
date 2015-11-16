//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

//	M6 stores documents and documents contain unstructured text.
//	That's not very useful by itself, and so we add attributes
//	to a document. An attribute could be e.g. an ID or a title.
//	These attributes are stored together with the document.
//	Attributes are limited to 255 bytes each.

#pragma once

#include <string>
#include <map>
#include <set>
#include <vector>

#include "M6Lexicon.h"

class M6Databank;

// Documents can have links:
typedef std::map<std::string,std::set<std::string>> M6DocLinks;

// Basic M6Document is only a mere interface

class M6Document
{
  public:
						M6Document(M6Databank& inDatabank);
	virtual 			~M6Document();
	
	virtual std::string	GetText() = 0;
	virtual std::string	GetAttribute(const std::string& inName) = 0;

	virtual M6DocLinks&	GetLinks() = 0;

  protected:

	M6Databank&			mDatabank;
	M6DocLinks			mLinks;
	
  private:
						M6Document(const M6Document&);
	M6Document&			operator=(const M6Document&);
};

// Input document, used to create documents that need to be inserted into the databank

class M6InputDocument : public M6Document
{
  public:

	typedef std::map<std::string,std::string>	M6DocAttributes;
	typedef std::vector<uint32>					M6TokenList;
	
	struct M6IndexTokens
	{
		M6DataType		mDataType;
		std::string		mIndexName;
		M6TokenList		mTokens;
	};
	
	typedef std::vector<M6IndexTokens>			M6IndexTokenList;
	
	struct M6IndexValue
	{
		M6DataType		mDataType;
		std::string		mIndexName;
		std::string		mIndexValue;
		double			mIndexFloatValue;
		bool			mUnique;
	};
	
	typedef std::vector<M6IndexValue>			M6IndexValueList;

						M6InputDocument(M6Databank& inDatabank);
						M6InputDocument(M6Databank& inDatabank,
							const std::string& inText);

	void				SetText(const std::string& inText);
	virtual std::string	GetText();
	const std::string&	Peek() const							{ return mText; }

	void				SetFasta(const std::string& inFasta)	{ mFasta = inFasta; }
	const std::string&	GetFasta() const						{ return mFasta; }
	
	virtual std::string	GetAttribute(const std::string& inName);
	
	void				SetAttribute(const std::string& inName,
							const char* inText, size_t inSize);

	virtual void		Index(const std::string& inIndex,
							M6DataType inDataType, bool isUnique,
							const char* inText, size_t inSize);
	void				Index(const std::string& inIndex, bool isUnique, double inValue);

	virtual void		Index(const std::string& inIndex,
							const std::vector<std::pair<const char*,size_t>>& inWords);

	virtual void		AddLink(const std::string& inDatabank, const std::string& inValue);

//	virtual void		IndexSequence(const std::string& inIndex, uint32 inWordSize,
//							const char* inSequence, size_t inLength);

	virtual void		Tokenize(M6Lexicon& inLexicon, uint32 inLastStopWord);
	virtual void		RemapTokens(const uint32 inTokenMap[]);

	void				Compress();
	void				Store();
	
	uint32				GetDocNr() const					{ return mDocNr; }
	
	const M6IndexTokenList& GetIndexTokens() const			{ return mTokens; }
	const M6IndexValueList& GetIndexValues() const			{ return mValues; }
	virtual M6DocLinks&	GetLinks()							{ return mLinks; }

  private:

	M6IndexTokenList::iterator
						GetIndexTokens(const std::string& inIndexName,
							M6DataType inDataType);

	std::string			mText;
	std::string			mFasta;
	std::vector<char>	mBuffer;
	M6DocAttributes		mAttributes;
	M6IndexTokenList	mTokens;
	M6IndexValueList	mValues;
	M6Lexicon			mDocLexicon;
	uint32				mDocNr;
};

// Output document, this is returned by the M6Databank object

class M6OutputDocument : public M6Document
{
  public:
						M6OutputDocument(M6Databank& inDatabank,
							uint32 inDocNr, uint32 inDocPage, uint32 inDocSize);

	virtual std::string	GetText();
	virtual std::string	GetAttribute(const std::string& inName);

	virtual M6DocLinks&	GetLinks();
	
  private:
	uint32				mDocNr, mDocPage, mDocSize;
	bool				mLinksRead;
};
