#pragma once

#include <string>
#include <map>
#include <vector>

#include "M6Lexicon.h"

enum M6IndexKind
{
	eM6ValueIndex,
	eM6TextIndex,
	eM6NumberIndex,
	
};

class M6Document
{
  public:
						M6Document();
	virtual 			~M6Document();
	
	void				SetID(const std::string& inID)		{ mID = inID; }
	const std::string&	GetID() const						{ return mID; }

	void				SetText(const std::string& inText)	{ mText = inText; }
	const std::string&	GetText() const						{ return mText; }
	
	void				SetMetaData(const std::string& inName,
							const std::string& inData)
						{
							mMetaData[inName] = inData;
						}
	
	const std::string&	GetMetaData(const std::string& inName)
						{
							return mMetaData[inName];
						}

	virtual void		IndexText(const std::string& inIndex, M6IndexKind inIndexKind,
							const std::string& inText, bool inIndexNumbers);

	virtual void		Tokenize(M6Lexicon& inLexicon);
	
	template<class Archive>
	Archive&			serialize(Archive& ar);
	
  private:

	struct M6TokenData
	{
		uint32			mDocToken;
		uint32			mGlobalToken;
	};
	
	typedef std::vector<M6TokenData>		M6TokenDataList;
	
	struct M6IndexTokens
	{
		M6IndexKind		mIndexKind;
		std::string		mIndexName;
		M6TokenDataList	mTokens;
	};
	
	typedef std::vector<M6IndexTokens>		M6IndexTokenList;

	M6IndexTokenList::iterator
						GetIndexTokens(const std::string& inIndexName,
							M6IndexKind inIndexKind);

	typedef std::map<std::string,std::string>	M6MetaData;

	std::string			mText, mID;
	M6MetaData			mMetaData;
	M6IndexTokenList	mTokens;
	M6Lexicon			mDocLexicon;
};
