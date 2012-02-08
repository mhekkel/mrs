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

	enum M6ProcessorType
	{
		eM6ProcessNone,
		eM6ProcessDelimited,
		eM6ProcessFixedWidth,
		eM6ProcessRegex
	};

	struct M6PostProcessor
	{
		boost::regex			what;
		std::string				with;
		bool					global;
	};
	
	typedef std::vector<M6PostProcessor> M6PostProcessors;

	struct M6Processor
	{
		std::string				key;
		std::string				name;
		M6DataType				type;
		bool					attr;
		bool					index;
		bool					unique;
		bool					stop;
		M6PostProcessors		post;
	};

	typedef std::vector<M6Processor> M6Processors;

	void				Glob(zeep::xml::element* inSource,
							std::vector<boost::filesystem::path>& outFiles);
	void				Process(const std::string& inDocument);
	void				Parse(const boost::filesystem::path& inFile);

	void				SetupProcessor(zeep::xml::element* inConfig);

	zeep::xml::element*	mConfig;
	M6Databank*			mDatabank;
	M6Lexicon			mLexicon;
	
	// parsing info
	M6ProcessorType		mProcessorType;
	M6Processors		mProcessors;
	boost::regex		mProcessorRE;
};
