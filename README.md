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

# Indexing Databanks

As an example, we will index the source code for MRS. First go to the Admin
pages and click the 'add' button in the Databanks tab page. Select the newly
created databank (first of the list) and enter an ID (e.g. m6src) and check
the Enabled checkbox.

Then you enter the path to the source files. If the path you enter is a
relative path, MRS will prepend it with the 'raw files' directory specified
in the Main tab. If the path is absolute however, it will take this path
directly. So we enter for Source files:

    /home/me/mrs-6.0.0/*.{cpp,h,inl,c}

Now if you tick the Recursive checkbox, MRS will include all files that have
a name that ends with either .cpp, .h, .inl or .c found in any directory
under /home/me/mrs-6.0.0/.

As parser use the generic parser. This parser simply indexes all text and
assigns an incrementing number as ID to each document. It assumes each file
is a document.

Now go the Main tab and click the Restart button (to save the configuration
file) and in a terminal type

    mrs build m6src

Restart the server again to load the newly created databank and you'll see
the m6src is now searchable.

# Building MRS

You need gcc 4.6, log4cpp >= 5 and boost >= 1.65 to build MRS. Intel compilers are supported
and Visual Studio 2010 is OK too.

## Docker

MRS comes with a Dockerfile for building mrs. It can also be used in
production. To build the docker image, run the following in the root project
folder:

    docker build -t mrs .

To run the container in development mode, allowing you to change the source on
the host (your machine) and have the changes picked up in the container, run:

    docker run -v /home/jon/projects/mrs:/app -p 18090:18090 -it mrs

This will run interactively and the MRS web interface is accessible on the host
at [http://localhost:18090](http://localhost:18090).

Your production environment might differ so take the following with a pinch of
salt. Instead of mapping the source on the local machine, you probably want to
map the data so it's not destroyed if the container gets deleted.

    docker run -v /srv/mrs/data:/srv/mrs-data -p 18090:18090 mrs

This maps the data on the host at `/srv/mrs/data` to the mount point
`/srv/mrs-data` in the container, allowing mrs in the container to write data
to the host.

## Linux (manually)

You need to have the following to build MRS from source code:

- gcc 4.6
- libboost >= 1.65
- liblog4cpp-dev >= 5		( http://log4cpp.sourceforge.net/ )
- libperl-dev			Development files for Perl

Then run:

    ./configure
    make
    make install

You can set several options using the configure script, see ./configure --help
to see which options are available.

After installing you have a single executable called 'mrs'. This program works
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

    mrs server start

If this was successful you can now access the MRS website at the address of
the local machine and the port specified (default: 18090). Use your web
browser to go to this address and click the Admin link on the right top of
the page. Then follow the instructions there.

The default setup will automatically update the enabled databanks. You can
update databanks manually as well of course using the 'mrs update db' command.

## Windows

Building MRS on Windows is not trivial, don't try it unless you have
experience building Windows software using Visual Studio.

First of all, you need to have MSVC set up correctly to build 64 bit
executables. You then need to build the Boost libraries with static
runtime libraries. The mrs project file assumes you've installed Boost in
C:\Boost and you're using version 1.65. Also, make sure you build Boost
with zlib and bz2 support. The way I did it is, download boost, extract it,
run boostrap and then:

    bjam link=static runtime-link=static threading=multi address-model=64
         -sBZIP2_SOURCE="C:/Users/maarten/projects/bzip2-1.0.6"
         -sZLIB_SOURCE="C:/Users/maarten/projects/zlib-1.2.5"
         -sICU_PATH="C:/Users/maarten/projects/icu" stage install

Then you need to build a custom Perl interpreter. Download the source code
and edit the file perl-5.14.1/win32/Makefile uncomment the line containing:

    CCTYPE = MSVC100

and build and install perl using nmake. This should install a perl in C:\Perl
and here you can also file the perl514.dll file which you need to place in the
m6\msvc\x64\{Debug,Release} folders.

Libzeep is contained in the source tar but the solution file assumes it is
located in the same directory as the m6 directory. Either move it, or change
the paths in the m6 project file.
