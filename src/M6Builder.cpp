#include "M6Lib.h"

#include <iostream>
#include <memory>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/tr1/tuple.hpp>
#include <boost/timer/timer.hpp>

#include "M6DocStore.h"
#include "M6Error.h"
#include "M6Databank.h"
#include "M6Document.h"
#include "M6Builder.h"
#include "M6Config.h"

using namespace std;
using namespace std::tr1;
namespace zx = zeep::xml;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

namespace
{

M6IndexKind MapIndexKind(const string& inKind)
{
	M6IndexKind result = eM6NoIndex;
		 if (inKind == "fulltext")	result = eM6FullTextIndex;
	else if (inKind == "varchar")	result = eM6VarCharIndex;
	else if (inKind == "number")	result = eM6NumberIndex;
	else if (inKind == "date")		result = eM6DateIndex;
	return result;
}

M6IndexProcessingAction MapIndexAction(const string& inAction)
{
	M6IndexProcessingAction result = eM6IndexActionIgnore;
		 if (inAction == "stop") result = eM6IndexActionStop;
	else if (inAction == "store") result = eM6IndexActionStore;
	else if (inAction == "ignore") result = eM6IndexActionIgnore;
	return result;
}

}

M6Builder::M6Builder(const string& inDatabank)
	: mConfig(M6Config::Instance().LoadConfig(inDatabank))
	, mDatabank(nullptr)
{
}

M6Builder::~M6Builder()
{
	delete mDatabank;
}

void M6Builder::Glob(zx::element* inSource, vector<fs::path>& outFiles)
{
	if (inSource == nullptr)
		THROW(("No source specified for databank"));

	string source = inSource->content();
	ba::trim(source);
	
	if (inSource->get_attribute("type") == "path")
		outFiles.push_back(source);
	else
		THROW(("Unsupported source type"));
}


void M6Builder::Store(const string& inDocument)
{
	M6InputDocument* doc = new M6InputDocument(*mDatabank, inDocument);
	
	foreach (M6AttributeParser& p, mAttributes)
	{
		string value;
		
		if (p.repeat == eM6RepeatOnce)
		{
			boost::smatch m;
			boost::match_flag_type flags = boost::match_default | boost::match_not_dot_newline;
			if (boost::regex_search(inDocument, m, p.re, flags))
			{
				value = m[1];
				ba::trim(value);
			}
		}
		else if (p.repeat == eM6RepeatConcat)
		{
			string::const_iterator start = inDocument.begin();
			string::const_iterator end = inDocument.end();
			boost::match_results<string::const_iterator> what;
			boost::match_flag_type flags = boost::match_default | boost::match_not_dot_newline;
			
			while (regex_search(start, end, what, p.re, flags))
			{
				string v(what[1].first, what[1].second);
				ba::trim(v);
				if (not value.empty())
					value += ' ';
				value += v;
				start = what[0].second;
				flags |= boost::match_prev_avail;
				flags |= boost::match_not_bob;
			}
		}
		
		if (not value.empty())
			doc->SetAttribute(p.name, value);
	}

	if (not mKeyValueRE.empty())
	{
		const char* start = inDocument.c_str();
		const char* end = start + inDocument.length();
		boost::match_results<const char*> what;
		boost::match_flag_type flags = boost::match_default | boost::match_not_dot_newline;
		
		while (regex_search(start, end, what, p.re, flags))
		{
			string key(what[1].first, what[1].second);
			
			M6IndexAction action = mDefault.action;
			
			foreach (M6IndexParser& p, mIndices)
			{
				if (p.key == key)
				{
					if (p.action == eM6IndexActionStore)
					{
						doc->Index(p.index, p.kind, p.unique, what[2].first, what[2].second - what[2].first);
						action = eM6IndexActionIgnore;
					}
					break;
				}
			}
			
			if (p.action == eM6IndexActionStore)
				doc->Index(mDefault.index, mDefault.kind, mDefault.unique,
					what[2].first, what[2].second - what[2].first);
			else if (p.action == eM6IndexActionStop)
				break;
			
			start = what[0].second;
			flags |= boost::match_prev_avail;
			flags |= boost::match_not_bob;
		}
	}
	else
		THROW(("unsupported"));
	
	doc->Tokenize(mLexicon, 0);
	
	mDatabank->Store(doc);

	static int n = 0;
	if (++n % 1000 == 0)
	{
		cout << '.';
		if (n % 60000 == 0)
			cout << endl;
		else
			cout.flush();
	}
}

void M6Builder::Parse(const fs::path& inFile)
{
	fs::ifstream file(inFile, ios::binary);
	string line;
	
	// do we need to strip off a header?
	zx::element* header = mConfig->find_first("header-line");
	if (header != nullptr)
	{
		boost::regex he(header->content());
		
		for (;;)
		{
			getline(file, line);
			if (line.empty() and file.eof())
				break;
			if (boost::regex_match(line, he))
				continue;
			break;
		}
	}
	else
		getline(file, line);

	string document;

	zx::element* separator = mConfig->find_first("document-separator");
	if (separator == nullptr)	// one file per document
	{
		io::filtering_ostream out(io::back_inserter(document));
		io::copy(file, out);
		Store(document);
	}
	else
	{
		string separatorLine = separator->content();
		string separatorType = separator->get_attribute("type");
		
		if (separatorType == "last-line-equals" or separatorType.empty())
		{
			for (;;)
			{
				document += line + "\n";
				if (line == separatorLine)
				{
					Store(document);
					document.clear();
				}

				getline(file, line);
				if (line.empty() and file.eof())
					break;
			}

			if (not document.empty())
				cerr << "There was data after the last document" << endl;
		}
		else
			THROW(("Unknown document separator type"));
	}
}

void M6Builder::Build()
{
	boost::timer::auto_cpu_timer t;

	zx::element* file = mConfig->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));

	fs::path path = file->content();
	mDatabank = M6Databank::CreateNew(path.string());
	mDatabank->StartBatchImport(mLexicon);
	
	vector<fs::path> files;
	Glob(mConfig->find_first("source"), files);

	// prepare the attribute parsers
	foreach (zx::element* attr, mConfig->find("document-attributes/document-attribute"))
	{
		M6AttributeParser p = {
			attr->get_attribute("name"),
			boost::regex(attr->get_attribute("match")),
			attr->get_attribute("repeat"))
		};
		
		if (p.name.empty() or p.re.empty())
			continue;
		
		mAttributes.push_back(p);
	}
	
	// prepare the index processing actions
	mIndexProcessingType = eM6IndexProcessingTypeNone;
	zx::element* indexing = mConfig->find_first("indexing");
	if (indexing != nullptr)
	{
		string s = indexing->get_attribute("process");
		if (s == "line-delimited-key-value")
			mIndexProcessingType = eM6LineDelimitedKeyValue;
		else if (s == "line-fixed-width-key-value")
			mIndexProcessingType = eM6LineFixedWidthKeyValue;
		else if (not s.empty())
			THROW(("Invalid indexing process type"));
		
		if (mIndexProcessingType == eM6LineDelimitedKeyValue)
			mKeyValueDelimiter = indexing->get_attribute("separator");

		mDefaultAction = MapIndexAction(indexing->get_attribute("default-action"));
		
		foreach (zx::element* ip, indexing->find("index"))
		{
			M6IndexParser p = {
				ip->get_attribute("key"),
				MapIndexAction(ip->get_attribute("action")),
				ip->get_attribute("index"),
				MapIndexKind(ip->get_attribute("index")),
				ip->get_attribute("unique") == "true"
			};
			
			mIndices.push_back(p);
		}
	}
	
	foreach (fs::path& file, files)
	{
		if (not fs::exists(file))
		{
			cerr << "file missing: " << file << endl;
			continue;
		}
		
		Parse(file);
	}
	
	mDatabank->CommitBatchImport();
	cout << endl << "done" << endl;
}

