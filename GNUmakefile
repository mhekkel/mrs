# Makefile for mrs-6
#
#  Copyright Maarten L. Hekkelman, Radboud University 2012.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)

VERSION				= 6.1.4

include make.config

# Directories where MRS will be installed
BIN_DIR				?= /usr/local/bin
MAN_DIR				?= /usr/local/man

MRS_DATA_DIR		?= /srv/mrs-data/
MRS_LOG_DIR			?= /var/log/mrs/
MRS_RUN_DIR			?= /var/run/
MRS_ETC_DIR			?= /usr/local/etc/mrs/

MRS_PORT			?= 18090
MRS_BASE_URL		?= http://chelonium.cmbi.umcn.nl:$(MRS_PORT)/
MRS_USER			?= $(shell whoami)

PERL				?= $(which perl)

DEFINES				+= MRS_ETC_DIR='"$(MRS_ETC_DIR)"' \
					   MRS_USER='"$(MRS_USER)"' \
						MRS_CURRENT_VERSION='"$(VERSION)"'

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

UNIT_TESTS			= unit_test_blast unit_test_query unit_test_exec
TESTS				= $(UNIT_TESTS)

VPATH += src unit-tests

OBJECTS = \
	$(OBJDIR)/M6BitStream.o \
	$(OBJDIR)/M6Blast.o \
	$(OBJDIR)/M6BlastCache.o \
	$(OBJDIR)/M6Builder.o \
	$(OBJDIR)/M6CmdLineDriver.o \
	$(OBJDIR)/M6Config.o \
	$(OBJDIR)/M6Log.o \
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

all: mrs config/mrs-config.xml mrs.1 init.d/mrs $(TESTS)

checkcache: $(OBJDIR)/checkcache.o
	$(CXX) -o $@ -I. $< $(LDFLAGS)

mrs: $(OBJECTS)
	@ echo "$(CXX) -o $@ -I. $^ $(LDFLAGS)"
	@ $(CXX) -o $@ -I. $^ $(LDFLAGS)

unit_test_blast: $(OBJDIR)/M6TestBlast.o $(OBJDIR)/M6Blast.o \
		$(OBJDIR)/M6Matrix.o $(OBJDIR)/M6Error.o $(OBJDIR)/M6Progress.o \
		$(OBJDIR)/M6Utilities.o $(OBJDIR)/M6Log.o $(OBJDIR)/M6Config.o
	$(CXX) -o $@ $^ $(LDFLAGS)

unit_test_query:  $(OBJDIR)/M6TestQuery.o $(OBJDIR)/M6Query.o \
		$(OBJDIR)/M6Databank.o $(OBJDIR)/M6Iterator.o $(OBJDIR)/M6BitStream.o \
		$(OBJDIR)/M6Tokenizer.o $(OBJDIR)/M6Error.o $(OBJDIR)/M6Index.o \
		$(OBJDIR)/M6File.o $(OBJDIR)/M6Progress.o $(OBJDIR)/M6DocStore.o \
		$(OBJDIR)/M6Document.o $(OBJDIR)/M6Lexicon.o $(OBJDIR)/M6Dictionary.o \
		$(OBJDIR)/M6Utilities.o
	$(CXX) -o $@ $^ $(LDFLAGS)

unit_test_exec: $(OBJDIR)/M6TestExec.o $(OBJDIR)/M6Exec.o $(OBJDIR)/M6Error.o \
		$(OBJDIR)/M6Server.o $(OBJDIR)/M6Utilities.o $(OBJDIR)/M6Log.o $(OBJDIR)/M6Parser.o \
		$(OBJDIR)/M6Databank.o $(OBJDIR)/M6Iterator.o $(OBJDIR)/M6BitStream.o $(OBJDIR)/M6Tokenizer.o \
		$(OBJDIR)/M6Builder.o $(OBJDIR)/M6Document.o $(OBJDIR)/M6Config.o $(OBJDIR)/M6Query.o \
		$(OBJDIR)/M6BlastCache.o $(OBJDIR)/M6WSSearch.o $(OBJDIR)/M6WSBlast.o $(OBJDIR)/M6Lexicon.o \
		$(OBJDIR)/M6DocStore.o $(OBJDIR)/M6DataSource.o $(OBJDIR)/M6File.o $(OBJDIR)/M6Dictionary.o \
		$(OBJDIR)/M6Index.o $(OBJDIR)/M6Progress.o $(OBJDIR)/M6Blast.o $(OBJDIR)/M6Matrix.o
	$(CXX) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@ echo ">>" $<
	@ $(CXX) -MD -c -o $@ $< -I src $(CFLAGS) $(CXXFLAGS)

$(OBJDIR)/M6Config.o: make.config

unicode/M6UnicodeTables.h src/../unicode/M6UnicodeTables.h:
	cd unicode; $(PERL) unicode-table-creator.pl > $(@F)

include $(OBJECTS:%.o=%.d)

$(OBJECTS:.o=.d):

$(OBJDIR):
	@ test -d $@ || mkdir -p $@

clean:
	rm -rf $(OBJDIR)/* mrs config/mrs-config.xml
	
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

install: mrs config/mrs-config.xml mrs.1 init.d/mrs logrotate.d/mrs
	@ echo "Creating directories"
	@ for d in $(INSTALLDIRS); do \
		if [ ! -d $$d ]; then \
			install -m755 -d $$d; \
		fi \
	done
	@ for d in `find docroot -type d | grep -v .svn`; do \
		install -m755 -d $(MRS_DATA_DIR)/$$d; \
	done
	@ echo "Copying files"
	@ for f in `find docroot -type f | grep -v .svn`; do \
		install -m644 $$f $(MRS_DATA_DIR)/$$f; \
	done
	@ for f in `find parsers -type f | grep -v .svn`; do \
		install -m644 $$f $(MRS_DATA_DIR)/$$f; \
	done
	install -m755 mrs $(BIN_DIR)/mrs
	install mrs.1 $(MAN_DIR)/man1/mrs.1; gzip -f $(MAN_DIR)/man1/mrs.1
	install -m444 config/mrs-config.dtd $(MRS_ETC_DIR)/mrs-config.dtd
	install -m644 config/mrs-config.xml $(MRS_ETC_DIR)/mrs-config.xml.dist
	@ if [ ! -f $(MRS_ETC_DIR)/mrs-config.xml ]; then \
		install -m644 config/mrs-config.xml $(MRS_ETC_DIR)/mrs-config.xml; \
		echo ""; \
		echo ""; \
		echo "     PLEASE NOTE"; \
		echo ""; \
		echo "Don't forget to create an admin user for the MRS server by running the command $(BIN_DIR)/mrs password"; \
		echo ""; \
		echo ""; \
	  else \
	    echo ""; \
	    echo "Not overwriting existing $(MRS_ETC_DIR)/mrs-config.xml"; \
	    echo "check the file $(MRS_ETC_DIR)/mrs-config.xml.dist for changes"; \
	    echo ""; \
	fi
	@ if [ ! -f /etc/init.d/mrs ]; then \
		install init.d/mrs /etc/init.d/mrs ; \
	  else \
		echo ""; \
		echo "Not overwriting /etc/init.d/mrs file" ; \
	  fi
	@ if [ ! -f /etc/logrotate.d/mrs ]; then \
		install -m644 logrotate.d/mrs /etc/logrotate.d/mrs ; \
	  else \
		echo ""; \
		echo "Not overwriting /etc/logrotate.d/mrs file" ; \
	  fi

DIST = mrs-$(VERSION)

dist:
	rm -rf $(DIST)
	svn export . $(DIST)
	rm -rf $(DIST)/unicode/CaseFolding.txt $(DIST)/unicode/UnicodeData.txt
	tar cvjf $(DIST).tbz $(DIST)/
	rm -rf $(DIST)

distclean:
	
make.config:
	@ echo "Please run configure before running make"
