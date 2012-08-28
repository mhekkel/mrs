#pragma once

#include <vector>
#include <string>

int ForkExec(std::vector<const char*>& args, double maxRunTime,
	const std::string& stdin, std::string& stdout, std::string& stderr);

