#pragma once

#include <vector>
#include <string>

int ForkExec(std::vector<const char*>& args, double maxRunTime,
	const std::string& in, std::string& out, std::string& err);

class M6Process
{
  public:
	
				M6Process(const std::vector<const char*>& args);
				~M6Process();

    bool		filter(const char*& begin_in, const char* end_in,
	                 char*& begin_out, char* end_out, bool flush);
    void		close();

  private:
				M6Process(const M6Process&);
	M6Process&	operator=(const M6Process&);

	struct M6ProcessImpl*	mImpl;
};
