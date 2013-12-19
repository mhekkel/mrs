//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <exception>
#include <boost/current_function.hpp>

class M6Exception : public std::exception
{
  public:
				M6Exception(const char* inMessage, ...);

	virtual const char* what() const throw();

	static const char*	last_what();

  protected:
				M6Exception();

	char		mMessage[512];
};

#if DEBUG

void			ReportThrow(const char* inFunction, const char* inFile, int inLine);

#define	THROW(a)	do { \
						ReportThrow(BOOST_CURRENT_FUNCTION, __FILE__, __LINE__); \
						throw M6Exception a; \
					} while (false)

void print_debug_message(const char* message, ...);
#define PRINT(m)	do { print_debug_message m; } while (false)

#else /* ! DEBUG */

#define THROW(a)	throw M6Exception a
#define PRINT(m)	

#endif
