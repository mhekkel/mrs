# Makefile for m6
#
#  Copyright Maarten L. Hekkelman, Radboud University 2008-2012.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)

VERSION				= 6.0.0b1

include make.config

# Directories where MRS will be installed
BINDIR				?= /usr/local/bin
MANDIR				?= /usr/local/man/man3

MRSBASEURL			= http://localhost:18090/
MRSDIR				?= /data
MRSLOGDIR			?= /var/log/mrs
MRSETCDIR			?= /usr/local/etc/mrs
#MRSUSER				?= dba

PERL				?= /usr/bin/perl

# in case you have boost >= 1.48 installed somewhere else on your disk
#BOOST_LIB_SUFFIX	= # e.g. '-mt', not usually needed anymore
BOOST				= $(HOME)/projects/boost
BOOST_LIB_DIR		= $(BOOST)/lib
BOOST_INC_DIR		= $(BOOST)/include

BOOST_LIBS			= system thread filesystem regex math_c99 math_c99f program_options date_time iostreams chrono timer random
BOOST_LIBS			:= $(BOOST_LIBS:%=boost_%$(BOOST_LIB_SUFFIX))
LIBS				= m pthread rt z bz2

CXX					?= c++

CXXFLAGS			+= -std=c++0x
CFLAGS				+= $(BOOST_INC_DIR:%=-I%) -I. -pthread -I libzeep/
CFLAGS				+= -Wno-deprecated -Wno-multichar 
CFLAGS				+= $(shell $(PERL) -MExtUtils::Embed -e perl_inc)

LDFLAGS				+= $(LIBS:%=-l%) $(BOOST_LIB_DIR:%=-L %) $(BOOST_LIBS:%=-l%) -g -L libzeep/ 
LDFLAGS				+= $(shell $(PERL) -MExtUtils::Embed -e ldopts)

OBJDIR				= obj

ifneq ($(DEBUG),1)
CFLAGS				+= -O3 -DNDEBUG -g
else
CFLAGS				+= -g -DDEBUG 
OBJDIR				:= $(OBJDIR).dbg
endif

ifeq ($(PROFILE),1)
CFLAGS				+= -pg
LDFLAGS				+= -pg
OBJDIR				:= $(OBJDIR).profile
endif

VPATH += src

OBJECTS = \
	$(OBJDIR)/M6BitStream.o \
	$(OBJDIR)/M6Blast.o \
	$(OBJDIR)/M6BlastCache.o \
	$(OBJDIR)/M6Builder.o \
	$(OBJDIR)/M6CmdLineDriver.o \
	$(OBJDIR)/M6Config.o \
	$(OBJDIR)/M6Databank.o \
	$(OBJDIR)/M6DataSource.o \
	$(OBJDIR)/M6Dictionary.o \
	$(OBJDIR)/M6DocStore.o \
	$(OBJDIR)/M6Document.o \
	$(OBJDIR)/M6Error.o \
	$(OBJDIR)/M6Exec.o \
	$(OBJDIR)/M6Fetch.o \
	$(OBJDIR)/M6File.o \
	$(OBJDIR)/M6Index.o \
	$(OBJDIR)/M6Iterator.o \
	$(OBJDIR)/M6Lexicon.o \
	$(OBJDIR)/M6Matrix.o \
	$(OBJDIR)/M6MD5.o \
	$(OBJDIR)/M6Parser.o \
	$(OBJDIR)/M6Progress.o \
	$(OBJDIR)/M6Query.o \
	$(OBJDIR)/M6SequenceFilter.o \
	$(OBJDIR)/M6Server.o \
	$(OBJDIR)/M6Tokenizer.o \
	$(OBJDIR)/M6Utilities.o \
	$(OBJDIR)/M6WSBlast.o \
	$(OBJDIR)/M6WSSearch.o \
	$(OBJDIR)/sqlite.o \

all: m6 config/m6-config.xml

m6: $(OBJECTS) libzeep/libzeep.a
	@ echo ">>" $@
	@ $(CXX) -o $@ -I. $^ $(LDFLAGS)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@ echo ">>" $<
	@ $(CXX) -MD -c -o $@ $< $(CFLAGS) $(CXXFLAGS)

$(OBJDIR)/sqlite.o: src/sqlite-amalgamation/sqlite3.c
	@ echo ">>" $<
	@ $(CXX) -x c -MD -c -o $@ $< $(CFLAGS)

include $(OBJECTS:%.o=%.d)

$(OBJECTS:.o=.d):

$(OBJDIR):
	@ test -d $@ || mkdir -p $@

libzeep/libzeep.a:
	$(MAKE) -C libzeep BOOST=$(BOOST) CXX=$(CXX)

clean:
	rm -rf $(OBJDIR)/* m6

config/m6-config.xml: config/m6-config.xml.dist
	sed -e 's|__DATA_DIR__|$(DATADIR)|g' \
		-e 's|__SCRIPT_DIR__|$(SCRIPTDIR)|g' \
		$@.dist > $@
	
INSTALLDIRS = $(MRSLOGDIR) $(MRSETCDIR) $(MRSDIR)/raw  $(MRSDIR)/mrs $(MRSDIR)/blast-cache \
	 $(MRSDIR)/docroot/scripts $(MRSDIR)/docroot/css $(MRSDIR)/docroot/formats $(MRSDIR)/docroot/images \
	 $(MRSDIR)/docroot/help 

install: m6
	for d in $(INSTALLDIRS); do \
		install $(MRSUSER:%=-o %) -m664 -d $$d; \
	done
	for f in `find docroot -type f | grep -v .svn`; do \
		install $(MRSUSER:%=-o %) -m664 $$f $(MRSDIR)/$$f; \
	done
	for f in `find parsers -type f | grep -v .svn`; do \
		install $(MRSUSER:%=-o %) -m664 $$f $(MRSDIR)/$$f; \
	done

DIST = m6-$(VERSION)
	
dist:
	rm -rf $(DIST)
	svn export . $(DIST)
	svn export ../libzeep $(DIST)/libzeep
	rm -rf $(DIST)/test $(DIST)/libzeep/tests
	tar cvjf $(DIST).tbz $(DIST)/
	rm -rf $(DIST)
	
make.config:
	@echo "creating empty make.config file"
	@echo "# Set local options for make here" > make.config
	@echo "#CXX			= $(HOME)/bin/c++			# at least version 4.6 of gcc or equivalent" >> make.config
	@echo "#BOOST		= $(HOME)/projects/boost	# at least version 1.48 of Boost" >> make.config
