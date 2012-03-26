#pragma once

#include <string>
#include <vector>

class M6Databank;
class M6Iterator;

void AnalyseQuery(const std::string& inQuery,
	std::vector<std::string>& outTerms);

void ParseQuery(M6Databank& inDatabank, const std::string& inQuery,
	bool inAllTermsRequired,
	std::vector<std::string>& outTerms, M6Iterator*& outFilter);
