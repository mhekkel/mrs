MRS is full text retrieval software optimized for large, flat file databanks
containing biological and medical information. After indexing these databanks,
they can be searched using a command line interface and when running the
server, using a web browser or using web services (SOAP, REST).

Searching can be done using simple keyword searches, but also using more
advanced boolean queries, searches limited to parts of a document, searches
for 'strings' (sequences of words). Results are sorted by relevance, when
applicable. MRS also has a protein BLAST search engine built in.

Previous versions used to be hard to set up and configure, version 6 tries
to address this by combining all configuration information in one place and
offering a graphical interface to manipulate this configuration. The update
process is fully automated and easy to follow.

MRS runs on both Linux and Windows although a recent C++ compiler is required
to build it. The Linux version comes with an init.d script. For windows, I 
still have to implement the code to run MRS as a service.

See the changelog file for an overview of what has changed since version 5.


Building MRS

You need gcc 4.6 and boost >= 1.48 to build MRS. Intel compilers are supported
and Visual Studio 2010 is OK too.

For Linux:

You need to have the following to build MRS from source code:

- gcc 4.6
- libboost >= 1.48
- libperl-dev			Development files for Perl

Then run:

./configure
make
make install

You can set several options using the configure script, see ./configure --help
to see which options are available.

After installing you have a single executable called 'm6'. This program works
with command as the first parameter followed by options depending on the
command.

  Command can be one of:

    blast       Do a blast search
    build       (Re-)build a databank
    dump        Dump index data
    entry       Retrieve and print an entry
    fetch       Fetch/mirror remote data for a databank
    info        Display information and statistics for a databank
    query       Perform a search in a databank
    server      Start or Stop a server session, or ask the status
    vacuum      Clean up a databank reclaiming unused disk space
    validate    Perform a set of validation tests
    update      Same as build, but does a fetch first
    password    Generate password for use in configuration file

The first thing you should do after installing MRS 6 is run the password
command and add a new user/password for the admin account. The password is
stored encrypted in the configuration file. You then start a server using
the command:

	m6 server start

If this was successful you can now access the MRS website at the address of
the local machine and the port specified (default: 18090). Use your web
browser to go to this address and click the Admin link on the right top of
the page. Then follow the instructions there.

The default setup will automatically update the enabled databanks. You can
update databanks manually as well of course using the 'm6 update db' command.



For Windows:

I've copied the text from the previous MRS readme here. It must be out of
date by now, I'll fix this asap.

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
