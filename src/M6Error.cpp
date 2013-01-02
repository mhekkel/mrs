//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#if defined(_MSC_VER)
#include <Windows.h>
#endif

#include <cstdarg>
#include <iostream>
#include <cstdio>
#include <cstring>

#include "M6Error.h"

using namespace std;

namespace
{

char sLastWhat[512];
	
}

M6Exception::M6Exception(const char* inMessage, ...)
{
	using namespace std;
	
	va_list vl;
	va_start(vl, inMessage);
	int n = vsnprintf(mMessage, sizeof(mMessage), inMessage, vl);
	va_end(vl);

#if defined(_MSC_VER)
    DWORD dw = ::GetLastError();
	if (dw != NO_ERROR)
	{
	    char* lpMsgBuf;
		int m = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&lpMsgBuf, 0, NULL);
	
		if (lpMsgBuf != nullptr)
		{
			// strip off the trailing whitespace characters
			while (m > 0 and isspace(lpMsgBuf[m - 1]))
				--m;
			lpMsgBuf[m] = 0;

			_snprintf(mMessage + n, sizeof(mMessage) - n, " (%s)", lpMsgBuf);

			::LocalFree(lpMsgBuf);
		}
	}
#endif

	// copy
	strcpy(sLastWhat, mMessage);

#if DEBUG
	cerr << mMessage << endl;
#endif
}

M6Exception::M6Exception()
{
}

const char* M6Exception::what() const throw()
{
	return mMessage;
}

const char* M6Exception::last_what()
{
	return sLastWhat;
}

#if DEBUG
void ReportThrow(const char* inFunc, const char* inFile, int inLine)
{
	cerr << endl << "Exception in " << inFunc << ", " << inFile << ':' << inLine << endl;
}

void print_debug_message(const char* inMessage, ...)
{
	using namespace std;
	
	va_list vl;
	va_start(vl, inMessage);
	vfprintf(stderr, inMessage, vl);
	va_end(vl);
	fputs("\n", stderr);
}

#endif
