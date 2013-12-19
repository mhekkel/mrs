//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <vector>
#include <iostream>

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/read.hpp>

int ForkExec(std::vector<const char*>& args, double maxRunTime,
	std::istream& in, std::ostream& out, std::ostream& err);

class M6Process : public boost::iostreams::source
{
  public:
	
	typedef char char_type;
	typedef boost::iostreams::source_tag category;

							M6Process(const std::string& inCommand,
								std::istream& inRawData);
							M6Process(const M6Process&);
	M6Process&				operator=(const M6Process&);
							~M6Process();

	std::streamsize			read(char* s, std::streamsize n);

  private:

	struct M6ProcessImpl*	mImpl;
};

