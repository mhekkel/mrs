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

	struct M6AttributeParser
	{
		std::string		name;
		boost::regex	re;
		std::string		repeat;
		M6IndexKind		index;
	};
	
	typedef std::vector<M6AttributeParser> M6AttributeParsers;

	void				Glob(zeep::xml::element* inSource,
							std::vector<boost::filesystem::path>& outFiles);
	void				Store(const std::string& inDocument);
	void				Parse(const boost::filesystem::path& inFile);

	zeep::xml::element*	mConfig;
	M6Databank*			mDatabank;
	M6Lexicon			mLexicon;
	M6AttributeParsers	mAttributes;
};
