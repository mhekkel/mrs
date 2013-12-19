//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <string>

class M6Progress
{
  public:
				M6Progress(const std::string& inDatabank,
					int64 inMax, const std::string& inAction);

				// indefinite version, shows ascii spinner
				M6Progress(const std::string& inDatabank,
					const std::string& inAction);
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
				~M6Status();

	static M6Status&
				Instance();
	
	bool		GetUpdateStatus(const std::string& inDatabank,
					std::string& outStage, float& outProgress);
	void		SetUpdateStatus(const std::string& inDatabank,
					const std::string& inStage, float inProgress);

	void		SetError(const std::string& inDatabank,
					const std::string& inErrorMessage);
	void		Cleanup(const std::string& inDatabank);

  private:

				M6Status();
				M6Status(const M6Status&);
	M6Status&	operator=(const M6Status&);

	struct M6StatusImpl*				mImpl;
};
