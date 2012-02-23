#pragma once

#include <string>
#include <vector>

class M6Databank;
class M6Iterator;

void ParseQuery(M6Databank& inDatabank, const std::string& inQuery,
	bool inAllTermsRequired,
	std::vector<std::string>& outQueryTerms, M6Iterator*& outIterator);
