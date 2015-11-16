//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <vector>
#include <string>
#include <tuple>

#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>
#include <zeep/xml/node.hpp>

#include "M6Lexicon.h"

class M6Databank;

class M6Builder
{
  public:
						M6Builder(const std::string& inDatabank);
						~M6Builder();
	
	void				Build(uint32 inNrOfThreads);

	bool				NeedsUpdate();

	static void			IndexDocument(const std::string& inDatabankID,
							M6Databank* inDatabank,
							const std::string& inText,
							const std::string& inFileName,
							std::vector<std::string>& outTerms);

  private:

	int64				Glob(boost::filesystem::path inRawDir,
							zeep::xml::element* inSource,
							std::vector<boost::filesystem::path>& outFiles);

	void				Parse(const boost::filesystem::path& inFile);

	const zeep::xml::element*
						mConfig;
	M6Databank*			mDatabank;
	M6Lexicon			mLexicon;
};

class M6Scheduler
{
  public:
	
	static M6Scheduler&	Instance();
	
	void				Schedule(const std::string& inDatabank,
							const char* inAction = "update");
	void				GetScheduledDatabanks(
							std::vector<std::string>& outDatabanks);
	
  private:

						M6Scheduler();
						~M6Scheduler();
						
						M6Scheduler(const M6Scheduler&);
	M6Scheduler&		operator=(const M6Scheduler&);

	void				Run();
	void				OpenBuildLog();

	boost::mutex		mLock;
	std::unique_ptr<std::ostream>
						mLogFile;
	boost::thread		mThread;
	std::deque<std::tuple<std::string,std::string>>
						mScheduled;
};
