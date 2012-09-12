#pragma once

#include <vector>
#include <string>

int ForkExec(std::vector<const char*>& args, double maxRunTime,
	const std::string& in, std::string& out, std::string& err);

