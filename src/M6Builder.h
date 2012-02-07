#pragma once

#include <vector>
#include <string>

#include <boost/filesystem/path.hpp>
#include <boost/regex.hpp>
#include <zeep/xml/node.hpp>

#include "M6Lexicon.h"

class M6Databank;

class M6Builder
{
  public:
						M6Builder(const std::string& inDatabank);
						~M6Builder();
	
	void				Build();

  private:

	enum M6AttributeRepeatOption
	{
		eM6RepeatOnce,
		eM6RepeatConcat
	};

	struct M6AttributeParser
	{
		std::string				name;
		boost::regex			re;
		M6AttributeRepeatOption	repeat;
	};
	
	typedef std::vector<M6AttributeParser> M6AttributeParsers;

	enum M6IndexProcessingType
	{
		eM6IndexProcessingTypeNone,
		eM6LineDelimitedKeyValue,
		eM6LineFixedWidthKeyValue
	};

	enum M6IndexProcessingAction
	{
		eM6IndexActionStop,
		eM6IndexActionIgnore,
		eM6IndexActionStoreText,
		eM6IndexActionStoreValue,
		eM6IndexActionStoreDate,
		eM6IndexActionStoreNumber
	};

	struct M6IndexAction
	{
		std::string				key;
		M6IndexProcessingAction	action;
		std::string				index;
		M6IndexKind				kind;
		bool					unique;
	};

	typedef std::vector<M6IndexParser> M6IndexParsers;
	
	void				Glob(zeep::xml::element* inSource,
							std::vector<boost::filesystem::path>& outFiles);
	void				Store(const std::string& inDocument);
	void				Parse(const boost::filesystem::path& inFile);

	zeep::xml::element*	mConfig;
	M6Databank*			mDatabank;
	M6Lexicon			mLexicon;
	
	// parsing info
	M6AttributeParsers	mAttributes;
	M6IndexProcessingType
						mIndexProcessingType;
	M6IndexProcessingAction
						mDefaultAction;
	boost::regex		mKeyValueRE;
	M6IndexParsers		mIndices;
};
