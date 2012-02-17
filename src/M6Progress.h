#pragma once

#include <string>

class M6Progress
{
  public:
				M6Progress(int64 inMax);
	virtual		~M6Progress();
	
	void		Update(int64 inConsumed, const std::string& inMessage);
	void		Consumed(int64 inConsumed);	// consumed is relative
	void		Progress(int64 inProgress);	// progress is absolute
	void		Message(const std::string& inMessage);

  private:
				M6Progress(const M6Progress&);
	M6Progress&	operator=(const M6Progress&);

	struct M6ProgressImpl*	mImpl;
};
