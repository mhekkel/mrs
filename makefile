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
#BOOST_LIB_DIR		= $(HOME)/projects/boost/lib
#BOOST_INC_DIR		= $(HOME)/projects/boost/include

PREFIX				?= /usr/local
LIBDIR				?= $(PREFIX)/lib
INCDIR				?= $(PREFIX)/include
MANDIR				?= $(PREFIX)/man/man3

BOOST_LIBS			= system thread filesystem regex math_c99 math_c99f program_options iostreams
BOOST_LIBS			:= $(BOOST_LIBS:%=boost_%$(BOOST_LIB_SUFFIX))
LIBS				= m pthread z
LDFLAGS				+= $(BOOST_LIB_DIR:%=-L%) $(LIBS:%=-l%) -g $(BOOST_LIBS:%=/usr/lib/lib%.a) \
						/usr/lib/gcc/x86_64-linux-gnu/4.6/libstdc++.a

CC					?= c++
CFLAGS				+= $(BOOST_INC_DIR:%=-I%) -I. -pthread -std=c++0x
ifneq ($(DEBUG),1)
CFLAGS				+= -O2 -DNDEBUG
else
CFLAGS				+= -g -DDEBUG 
LDFLAGS				+= -g
endif

VPATH += src

OBJECTS = \
	obj/M6Databank.o \
	obj/M6DocStore.o \
	obj/M6Document.o \
	obj/M6Error.o \
	obj/M6FastLZ.o \
	obj/M6File.o \
	obj/M6Index.o \
	obj/M6Lexicon.o \
	obj/M6Tokenizer.o \

m6-build: $(OBJECTS) obj/M6Builder.o
	$(CC) $(BOOST_INC_DIR:%=-I%) -o $@ -I. $^ $(LDFLAGS)

m6-test: $(OBJECTS) obj/M6TestMain.o obj/M6TestDocStore.o
	$(CC) $(BOOST_INC_DIR:%=-I%) -o $@ -I. $^ $(LDFLAGS) /usr/lib/libboost_unit_test_framework.a

obj/%.o: %.cpp
	$(CC) -MD -c -o $@ $< $(CFLAGS)

include $(OBJECTS:%.o=%.d)

$(OBJECTS:.o=.d):

clean:
	rm -rf obj/* m6-build m6-create m6-test
