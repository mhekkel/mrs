You need gcc 4.6 and boost >= 1.48 to build M6. Intel compilers are supported and Visual Studio 2010 is OK too.

For Linux:

You need to have the following to build MRS from source code:

- gcc 4.6
- libboost >= 1.48
	(1.46 is possible, but you have to modify the makefile in that case)
- libperl-dev			Development files for Perl
- sqlite development files for version 3
- libzeep 2.9			can be downloaded from
							https://svn.cmbi.ru.nl/libzeep/trunk/
- libarchive development files

There's no configuration script anymore, just type make.

For Windows:

I've copied the text from the previous MRS readme here, hope it is still correct :-)

To build MRS using Microsoft Visual Studio 2010 you need the following:

- zlib
- libbz2
- perl
- boost

If you don't have MS Studio 2010, you can use the Express version instead. However, this means you will build a 32 bit version of MRS and of course that means you're limited to indexing only smaller databanks (you won't be able to index EMBL or even TrEMBL e.g.). You still can use large files created on other computers though.

Directory layout

The way I usually set-up my projects is by creating a directory called 'projects' first. This directory is located in my home directory. So in my case, the projects directory is

C:\Users\maarten\projects\

Since you're reading this document you probably have extracted the source code already, but if you didn't, save the code in a directory called 'm6' in this projects directory.

The next thing you need to do is download the source code for both libz and libbz2. Extract both into the same projects directory.

We now need a custom build perl. Download the perl source code (when writing this readme I'm using perl-5.14.1). Extract this code into the projects directory as well. Now edit the file perl-5.14.1/win32/Makefile and change the line containing

	CCTYPE		= MSVC100

removing the leading hash comment character. In case you're using the Express edition on a 64 bit version of Windows, you need to uncomment the line containing 'WIN64 = undef'.

Now open the command-line prompt for the MS Visual tools, a shortcut can be found in the Start menu/Microsoft Visual Studio 2010/Visual Studio Tools. cd into the win32 directory of perl-5.14.1 and type nmake and nmake install. This should install your new perl in C:\perl. Be sure to use the 64 bit version of the shortcut.

The next step is to build boost. Begin by downloading e.g. boost-1_46_1 and bjam. Install the bjam executable somewhere in your path and extract the boost tar file in the projects directory. In the extracted boost directory you run the following bjam command to build the libraries. Be sure the paths in this command are correct (for zlib and bzlib).

bjam link=static runtime-link=static threading=multi address-model=64 -sBZIP2_SOURCE="C:/Users/maarten/projects/bzip2-1.0.6" -sZLIB_SOURCE="C:/Users/maarten/projects/zlib-1.2.5" -sICU_PATH="C:/Users/maarten/projects/icu" stage

bjam link=static runtime-link=static threading=multi address-model=64 -sBZIP2_SOURCE="C:/Users/maarten/projects/bzip2-1.0.6" -sZLIB_SOURCE="C:/Users/maarten/projects/zlib-1.2.5" stage install --prefix="C:/Users/maarten/projects/boost/"

The result should be a boost directory in the projects directory containing the right libraries and header files.

And now you're ready to open the m6.sln solution file located in msvc and build the Release version of MRS.
