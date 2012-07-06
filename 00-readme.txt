Assuming you're building m6 in a directory called projects in your home directory (/home/you/projects/m6).

You need gcc 4.6 and boost 1.46 to build M6. Intel compilers are supported too and Visual Studio 2010 are OK too.

Do NOT use the default libpcre code that comes with your OS. It usually is build such that it will certainly crash M6.

build PCRE with:

./configure --disable-stack-for-recursion --enable-utf8 --prefix=$HOME/projects/pcre
