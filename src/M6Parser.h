//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <boost/thread/tss.hpp>

#include "M6Document.h"

struct M6ParserImpl;
typedef boost::thread_specific_ptr<M6ParserImpl>	M6ParserImplPtr;

class M6Parser
{
  public:
					M6Parser(const std::string& inName);
					~M6Parser();

					// return the version string of the current data set.
					// creates a temporary parser object.
	std::string		GetVersion(const std::string& inSourceConfig);

	void			GetIndexNames(
						std::vector<std::pair<std::string,std::string>>& outIndexNames);

	void			ParseDocument(M6InputDocument* inDoc,
						const std::string& inFileName, const std::string& inDbHeader);
	std::string		GetValue(const std::string& inName);

	void			ToFasta(const std::string& inDoc, const std::string& inID,
						const std::string& inDb, const std::string& inTitle,
						std::string& outFasta);

  private:

	M6ParserImpl*	Impl();

	std::string		mName;
	M6ParserImplPtr	mImpl;
};
