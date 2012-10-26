#pragma once

#include <string>

class M6Progress
{
  public:
				M6Progress(const std::string& inDatabank,
					int64 inMax, const std::string& inAction);
	virtual		~M6Progress();
	
	void		Consumed(int64 inConsumed);	// consumed is relative
	void		Progress(int64 inProgress);	// progress is absolute

	void		Message(const std::string& inMessage);

  private:
				M6Progress(const M6Progress&);
	M6Progress&	operator=(const M6Progress&);

	struct M6ProgressImpl*	mImpl;
};

class M6Status
{
  public:
	static void	Create();

	static M6Status&	Instance();
	
	bool		GetUpdateStatus(const std::string& inDatabank,
					std::string& outStage, float& outProgress);
	void		SetUpdateStatus(const std::string& inDatabank,
					const std::string& inStage, float inProgress);

  private:

				M6Status(bool inServer);
				~M6Status();
				
				M6Status(const M6Status&);
	M6Status&	operator=(const M6Status&);

	struct M6StatusImpl*	mImpl;
	static M6Status*		sInstance;
};
