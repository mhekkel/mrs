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
	if (inKind == "value")
		result = eM6ValueIndex;
	else if (inKind == "text")
		result = eM6TextIndex;
	else if (inKind == "number")
		result = eM6NumberIndex;
	else if (inKind == "date")
		result = eM6DateIndex;
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
		
		if (p.repeat == "once")
		{
			boost::smatch m;
			boost::match_flag_type flags = boost::match_default | boost::match_not_dot_newline;
			if (boost::regex_search(inDocument, m, p.re, flags))
				value = m[1];
		}
		else if (p.repeat == "concatenate")
		{
			string::const_iterator start = inDocument.begin();
			string::const_iterator end = inDocument.end();
			boost::match_results<string::const_iterator> what;
			boost::match_flag_type flags = boost::match_default | boost::match_not_dot_newline;
			
			while (regex_search(start, end, what, p.re, flags))
			{
				string v(what[1].first, what[1].second);
				if (not value.empty())
					value += ' ';
				value += v;
				start = what[0].second;
				flags |= boost::match_prev_avail;
				flags |= boost::match_not_bob;
			}
		}
		
		switch (p.index)
		{
#pragma warning("TODO Dit moet beter:")
		case eM6ValueIndex:	doc->IndexValue(p.name, eM6ValueIndex, value); break;
			case eM6NoIndex:	break;
			default:			doc->IndexText(p.name, p.index, value, false); break;
		}
		
		if (not value.empty())
			doc->SetAttribute(p.name, value);
	}
	
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
			attr->get_attribute("repeat"),
			MapIndexKind(attr->get_attribute("index"))
		};
		
		if (p.name.empty() or p.re.empty())
			continue;
		
		mAttributes.push_back(p);
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

