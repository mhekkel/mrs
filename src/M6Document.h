//	M6 stores documents and documents contain unstructured text.
//	That's not very useful by itself, and so we add attributes
//	to a document. An attribute could be e.g. an ID or a title.
//	These attributes are stored together with the document.
//	Attributes are limited to 255 bytes each.

#pragma once

#include <string>
#include <map>
#include <vector>

#include "M6Lexicon.h"

class M6Databank;

// Basic M6Document is only a mere interface

class M6Document
{
  public:
						M6Document(M6Databank& inDatabank);
	virtual 			~M6Document();
	
	virtual std::string	GetText() = 0;
	virtual std::string	GetAttribute(const std::string& inName) = 0;

  protected:

	M6Databank&			mDatabank;
	
  private:
						M6Document(const M6Document&);
	M6Document&			operator=(const M6Document&);
};

// Input document, used to create documents that need to be inserted into the databank

class M6InputDocument : public M6Document
{
  public:
						M6InputDocument(M6Databank& inDatabank,
							const std::string& inText);

	virtual std::string	GetText();
	virtual std::string	GetAttribute(const std::string& inName);
	
	void				SetAttribute(const std::string& inName,
							const std::string& inData);

	//virtual void		IndexText(const std::string& inIndex, M6IndexKind inIndexKind,
	//						const std::string& inText, bool inIndexNumbers);

	virtual void		Tokenize(M6Lexicon& inLexicon);

	void				Store();

  private:

	typedef std::map<std::string,std::string>	M6DocAttributes;

	struct M6TokenData
	{
		uint32			mDocToken;
		uint32			mGlobalToken;
	};

	typedef std::vector<M6TokenData>			M6TokenDataList;
	
	struct M6IndexTokens
	{
		//M6IndexKind		mIndexKind;
		std::string		mIndexName;
		M6TokenDataList	mTokens;
	};
	
	typedef std::vector<M6IndexTokens>			M6IndexTokenList;

	//M6IndexTokenList::iterator
	//					GetIndexTokens(const std::string& inIndexName,
	//						M6IndexKind inIndexKind);

	std::string			mText;
	M6DocAttributes		mAttributes;
	M6IndexTokenList	mTokens;
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
	
  private:
	uint32				mDocNr, mDocPage, mDocSize;
};
