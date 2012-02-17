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

	int64				Glob(zeep::xml::element* inSource,
							std::vector<boost::filesystem::path>& outFiles);

	void				Parse(const boost::filesystem::path& inFile);

	zeep::xml::element*	mConfig;
	M6Databank*			mDatabank;
	M6Lexicon			mLexicon;
	
	// parsing info
//	M6ProcessorType		mProcessorType;
//	M6Processors		mProcessors;
//	boost::regex		mProcessorRE;
};
