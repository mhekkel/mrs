# Makefile for m6
#
#  Copyright Maarten L. Hekkelman, Radboud University 2012.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)

VERSION				= 6.0.2

include make.config

# Directories where MRS will be installed
BIN_DIR				?= /usr/local/bin
MAN_DIR				?= /usr/local/man

MRS_DATA_DIR		?= /srv/m6-data/
MRS_LOG_DIR			?= /var/log/m6/
MRS_RUN_DIR			?= /var/run/
MRS_ETC_DIR			?= /usr/local/etc/mrs/

MRS_PORT			?= 18090
MRS_BASE_URL		?= http://$(shell hostname -f):$(MRS_PORT)/
MRS_USER			?= $(shell whoami)

PERL				?= $(which perl)

DEFINES				+= MRS_ETC_DIR='"$(MRS_ETC_DIR)"' \
					   MRS_USER='"$(MRS_USER)"' 

BOOST_LIBS			= system thread filesystem regex math_c99 math_c99f program_options date_time iostreams timer random chrono
BOOST_LIBS			:= $(BOOST_LIBS:%=boost_%$(BOOST_LIB_SUFFIX))
LIBS				= m pthread rt z bz2 zeep

CXX					?= c++

CXXFLAGS			+= -std=c++0x
CFLAGS				+= $(INCLUDE_DIR:%=-I%) -I. -pthread
CFLAGS				+= -Wno-deprecated -Wno-multichar 
CFLAGS				+= $(shell $(PERL) -MExtUtils::Embed -e perl_inc)
CFLAGS				+= $(DEFINES:%=-D%)

LDFLAGS				+= $(LIBRARY_DIR:%=-L %) $(LIBS:%=-l%) $(BOOST_LIBS:%=-l%) -g 
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
	$(OBJDIR)/M6Server.o \
	$(OBJDIR)/M6Tokenizer.o \
	$(OBJDIR)/M6Utilities.o \
	$(OBJDIR)/M6WSBlast.o \
	$(OBJDIR)/M6WSSearch.o \

all: m6 config/m6-config.xml m6.1 init.d/m6

m6: $(OBJECTS)
	@ echo ">>" $@
	@ $(CXX) -o $@ -I. $^ $(LDFLAGS)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@ echo ">>" $<
	@ $(CXX) -MD -c -o $@ $< $(CFLAGS) $(CXXFLAGS)

$(OBJDIR)/M6Config.o: make.config

include $(OBJECTS:%.o=%.d)

$(OBJECTS:.o=.d):

$(OBJDIR):
	@ test -d $@ || mkdir -p $@

clean:
	rm -rf $(OBJDIR)/* m6 config/m6-config.xml
	
INSTALLDIRS = \
	$(BIN_DIR) \
	$(MAN_DIR)/man1 \
	$(MRS_LOG_DIR) \
	$(MRS_RUN_DIR) \
	$(MRS_ETC_DIR) \
	$(MRS_DATA_DIR)/raw \
	$(MRS_DATA_DIR)/mrs \
	$(MRS_DATA_DIR)/blast-cache \
	$(MRS_DATA_DIR)/parsers \
	$(MRS_DATA_DIR)/docroot

%: %.dist
	sed -e 's|__MRS_DATA_DIR__|$(MRS_DATA_DIR)|g' \
		-e 's|__BIN_DIR__|$(BIN_DIR)|g' \
		-e 's|__MRS_ETC_DIR__|$(MRS_ETC_DIR)|g' \
		-e 's|__MRS_LOG_DIR__|$(MRS_LOG_DIR)|g' \
		-e 's|__MRS_RUN_DIR__|$(MRS_RUN_DIR)|g' \
		-e 's|__MRS_USER__|$(MRS_USER)|g' \
		-e 's|__MRS_BASE_URL__|$(MRS_BASE_URL)|g' \
		-e 's|__MRS_PORT__|$(MRS_PORT)|g' \
		-e 's|__RSYNC__|$(RSYNC)|g' \
		-e 's|__CLUSTALO__|$(CLUSTALO)|g' \
		$< > $@

install: m6 config/m6-config.xml m6.1 init.d/m6 logrotate.d/m6
	@ echo "Creating directories"
	@ for d in $(INSTALLDIRS); do \
		if [ ! -d $$d ]; then \
			install $(MRS_USER:%=-o %) -m755 -d $$d; \
		fi \
	done
	@ for d in `find docroot -type d | grep -v .svn`; do \
		install $(MRS_USER:%=-o %) -m755 -d $(MRS_DATA_DIR)/$$d; \
	done
	@ echo "Copying files"
	@ for f in `find docroot -type f | grep -v .svn`; do \
		install $(MRS_USER:%=-o %) -m644 $$f $(MRS_DATA_DIR)/$$f; \
	done
	@ for f in `find parsers -type f | grep -v .svn`; do \
		install $(MRS_USER:%=-o %) -m644 $$f $(MRS_DATA_DIR)/$$f; \
	done
	install -m755 m6 $(BIN_DIR)/m6
	install m6.1 $(MAN_DIR)/man1/m6.1; gzip -f $(MAN_DIR)/man1/m6.1
	ln -Tfs $(MAN_DIR)/man1/m6.1.gz $(MAN_DIR)/man1/mrs.1.gz
	install $(MRS_USER:%=-o %) -m444 config/m6-config.dtd $(MRS_ETC_DIR)/m6-config.dtd
	install $(MRS_USER:%=-o %) -m644 config/m6-config.xml $(MRS_ETC_DIR)/m6-config.xml.dist
	@ if [ ! -f $(MRS_ETC_DIR)/m6-config.xml ]; then \
		install $(MRS_USER:%=-o %) -m644 config/m6-config.xml $(MRS_ETC_DIR)/m6-config.xml; \
		echo ""; \
		echo ""; \
		echo "     PLEASE NOTE"; \
		echo ""; \
		echo "Don't forget to create an admin user for the MRS server by running the command $(BIN_DIR)/m6 password"; \
		echo ""; \
		echo ""; \
	  else \
	    echo ""; \
	    echo "Not overwriting existing $(MRS_ETC_DIR)/m6-config.xml"; \
	    echo "check the file $(MRS_ETC_DIR)/m6-config.xml.dist for changes"; \
	    echo ""; \
	fi
	@ if [ ! -f /etc/init.d/m6 ]; then \
		install init.d/m6 /etc/init.d/m6 ; \
	  else \
		echo ""; \
		echo "Not overwriting /etc/init.d/m6 file" ; \
	  fi
	@ if [ ! -f /etc/logrotate.d/m6 ]; then \
		install logrotate.d/m6 /etc/logrotate.d/m6 ; \
	  else \
		echo ""; \
		echo "Not overwriting /etc/logrotate.d/m6 file" ; \
	  fi

DIST = mrs-$(VERSION)

dist:
	rm -rf $(DIST)
	svn export . $(DIST)
	tar cvjf $(DIST).tbz $(DIST)/
	rm -rf $(DIST)
	cp $(DIST).tbz ../ppa/mrs_$(VERSION).orig.tar.bz2

distclean:
	
make.config:
	@ echo "Please run configure before running make"
