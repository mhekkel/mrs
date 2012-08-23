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

	void			ParseDocument(M6InputDocument* inDoc);
	std::string		GetValue(const std::string& inName);

  private:
	std::string		mName;
	M6ParserImplPtr	mImpl;
};
