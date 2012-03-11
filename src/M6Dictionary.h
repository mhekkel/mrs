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
				std::vector<std::string>& outCorrections);

	void	SuggestSearchTerms(const std::string& inWord,
				std::vector<std::string>& outSearchTerms);

	static void Create(M6BasicIndex& inIndex, uint32 inDocCount,
				M6File& inFile, M6Progress& inProgress);

  private:
	M6Automaton*	mAutomaton;
};
