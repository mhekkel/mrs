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

PREFIX				?= /usr/local
LIBDIR				?= $(PREFIX)/lib
INCDIR				?= $(PREFIX)/include
MANDIR				?= $(PREFIX)/man/man3

OBJDIR				= obj

BOOST_LIBS			= system thread filesystem regex math_c99 math_c99f program_options iostreams
BOOST_LIBS			:= $(BOOST_LIBS:%=boost_%$(BOOST_LIB_SUFFIX))
LIBS				= m pthread z zeep
LDFLAGS				+= $(BOOST_LIB_DIR:%=-L%) $(LIBS:%=-l%) -g $(BOOST_LIBS:%=$(BOOST_LIB_DIR)/lib%.a) \
							-L ../libzeep/

CC					= icpc
CFLAGS				+= $(BOOST_INC_DIR:%=-I%) -I. -pthread -std=c++0x -I../libzeep/
ifneq ($(DEBUG),1)
CFLAGS				+= -O3 -DNDEBUG
else
CFLAGS				+= -g -DDEBUG 
LDFLAGS				+= -g
OBJDIR				:= $(OBJDIR).dbg
endif

VPATH += src

OBJECTS = \
	$(OBJDIR)/M6BitStream.o \
	$(OBJDIR)/M6Builder.o \
	$(OBJDIR)/M6Config.o \
	$(OBJDIR)/M6Databank.o \
	$(OBJDIR)/M6DocStore.o \
	$(OBJDIR)/M6Document.o \
	$(OBJDIR)/M6Error.o \
	$(OBJDIR)/M6FastLZ.o \
	$(OBJDIR)/M6File.o \
	$(OBJDIR)/M6Index.o \
	$(OBJDIR)/M6Lexicon.o \
	$(OBJDIR)/M6Tokenizer.o \

m6: $(OBJECTS) $(OBJDIR)/M6CmdLineDriver.o
	$(CC) $(BOOST_INC_DIR:%=-I%) -o $@ -I. $^ $(LDFLAGS)

m6-test: $(OBJECTS) $(OBJDIR)/M6TestMain.o obj/M6TestDocStore.o
	$(CC) $(BOOST_INC_DIR:%=-I%) -o $@ -I. $^ $(LDFLAGS) $(BOOST_LIB_DIR)/libboost_unit_test_framework.a

$(OBJDIR)/%.o: %.cpp
	$(CC) -MD -c -o $@ $< $(CFLAGS)

include $(OBJECTS:%.o=%.d)

$(OBJECTS:.o=.d):

$(OBJDIR):
	@ test -d $@ || mkdir -p $@

clean:
	rm -rf $(OBJDIR)/* m6-build m6-create m6-test
