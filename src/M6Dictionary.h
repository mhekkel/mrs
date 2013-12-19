//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <vector>
#include <boost/filesystem/path.hpp>

#include "M6Index.h"

class M6Automaton;
class M6File;
class M6Progress;

class M6Dictionary
{
  public:
			M6Dictionary(boost::filesystem::path inFile);
			~M6Dictionary();

	void	SuggestCorrection(const std::string& inWord,
				std::vector<std::pair<std::string,uint16>>& outCorrections);

	void	SuggestSearchTerms(const std::string& inWord,
				std::vector<std::string>& outSearchTerms);

	static void Create(M6BasicIndex& inIndex, uint32 inDocCount,
				M6File& inFile, M6Progress& inProgress);

  private:
	M6Automaton*	mAutomaton;
};
