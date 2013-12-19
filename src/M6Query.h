//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <string>
#include <vector>

class M6Databank;
class M6Iterator;

void AnalyseQuery(const std::string& inQuery,
	std::vector<std::string>& outTerms);

void ParseQuery(M6Databank& inDatabank, const std::string& inQuery,
	bool inAllTermsRequired,
	std::vector<std::string>& outTerms, M6Iterator*& outFilter,
	bool& outIsBooleanQuery);
