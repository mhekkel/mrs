#include "M6Lib.h"

#include <iostream>

#include "M6MD5.h"

using namespace std;

int VERBOSE;

// --------------------------------------------------------------------

#if defined(_MSC_VER)
#include <Windows.h>

void SetStdinEcho(bool inEnable)
{
    HANDLE hStdin = ::GetStdHandle(STD_INPUT_HANDLE); 
    DWORD mode;
    ::GetConsoleMode(hStdin, &mode);

    if(not inEnable)
        mode &= ~ENABLE_ECHO_INPUT;
    else
        mode |= ENABLE_ECHO_INPUT;

    ::SetConsoleMode(hStdin, mode);
}
#endif

// --------------------------------------------------------------------
    
#if defined(linux) || defined(__linux) || defined (__linux__)
#include <termio.h>

void SetStdinEcho(bool inEnable)
{
    struct termios tty;
    ::tcgetattr(STDIN_FILENO, &tty);
    if(not inEnable)
        tty.c_lflag &= ~ECHO;
    else
        tty.c_lflag |= ECHO;

    (void)::tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}
#endif

// --------------------------------------------------------------------

int main(int argc, char* const argv[])
{
	string username, realm, password;
	
	cout << "Enter username: "; cout.flush();
	getline(cin, username);
	cout << "Enter realm:    "; cout.flush();
	getline(cin, realm);
	cout << "Enter password: "; cout.flush(); SetStdinEcho(false);
	getline(cin, password);
	SetStdinEcho(true);
	cout << endl << endl
		 << M6MD5(username + ':' + realm + ':' + password).Finalise() << endl;
	return 0;
}
