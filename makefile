# Makefile for m6 tools
#
#  Copyright Maarten L. Hekkelman, Radboud University 2008-2010.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)
#
# You may have to edit the first three defines on top of this
# makefile to match your current installation.

#BOOST_LIB_SUFFIX	= 				# e.g. '-mt'
BOOST_LIB_DIR		= $(HOME)/projects/boost/lib
BOOST_INC_DIR		= $(HOME)/projects/boost/include

PERL				?= /usr/bin/perl

PREFIX				?= /usr/local
LIBDIR				?= $(PREFIX)/lib
INCDIR				?= $(PREFIX)/include
MANDIR				?= $(PREFIX)/man/man3

OBJDIR				= obj

BOOST_LIBS			= system thread filesystem regex math_c99 math_c99f program_options date_time iostreams timer random
BOOST_LIBS			:= $(BOOST_LIBS:%=boost_%$(BOOST_LIB_SUFFIX))
LIBS				= m pthread bz2 z zeep rt
#LDFLAGS				+= $(BOOST_LIB_DIR:%=-L%) $(LIBS:%=-l%) -g $(BOOST_LIBS:%=$(BOOST_LIB_DIR)/lib%.a) \
#							-L ../libzeep/ $(HOME)/lib64/libstdc++.a
LDFLAGS				+= $(LIBS:%=-l%) $(BOOST_LIBS:%=-l%) -g -L ../libzeep/ 

CXX					= c++
CFLAGS				+= $(BOOST_INC_DIR:%=-I%) -I. -pthread -std=c++0x -I../libzeep/
CFLAGS				+= -Wno-deprecated -Wno-multichar 
CFLAGS				+= $(shell $(PERL) -MExtUtils::Embed -e perl_inc)
LDFLAGS				+= $(shell $(PERL) -MExtUtils::Embed -e ldopts)
LDFLAGS				+= $(shell curl-config --libs)
LDFLAGS				+= -lsqlite3
ifneq ($(DEBUG),1)
CFLAGS				+= -O3 -DNDEBUG -g
else
CFLAGS				+= -g -DDEBUG 
LDFLAGS				+= -g
OBJDIR				:= $(OBJDIR).dbg
endif

ifeq ($(PROFILE),1)
CFLAGS				+= -pg
LDFLAGS				+= -pg
OBJDIR				:= $(OBJDIR).Profile
endif

VPATH += src

OBJECTS = \
	$(OBJDIR)/M6BitStream.o \
	$(OBJDIR)/M6Blast.o \
	$(OBJDIR)/M6Builder.o \
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
	$(OBJDIR)/M6Tokenizer.o \
	$(OBJDIR)/M6Utilities.o \

OBJECTS.m6 = \
	$(OBJECTS) \
	$(OBJDIR)/M6BlastCache.o \
	$(OBJDIR)/M6CmdLineDriver.o \
	$(OBJDIR)/M6Server.o \
	$(OBJDIR)/M6WSBlast.o \
	$(OBJDIR)/M6WSSearch.o \

OBJECTS.m6-test = \
	$(OBJECTS) \
	$(OBJDIR)/M6TestMain.o \
	$(OBJDIR)/M6TestDocStore.o 

all: m6

m6: $(OBJECTS.m6)
	@ echo ">>" $@
	@ $(CXX) $(BOOST_INC_DIR:%=-I%) -o $@ -I. $^ $(LDFLAGS)

m6-test: $(OBJECTS.m6-test)
	@ echo ">>" $@
	@ $(CXX) $(BOOST_INC_DIR:%=-I%) -o $@ -I. $^ $(LDFLAGS) $(BOOST_LIB_DIR)/libboost_unit_test_framework.a

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@ echo ">>" $<
	@ $(CXX) -MD -c -o $@ $< $(CFLAGS)

include $(OBJECTS:%.o=%.d)

$(OBJECTS:.o=.d):

$(OBJDIR):
	@ test -d $@ || mkdir -p $@

clean:
	rm -rf $(OBJDIR)/* m6 m6-test
