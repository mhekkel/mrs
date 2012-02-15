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
//#include <boost/timer/timer.hpp>

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

M6DataType MapDataType(const string& inKind)
{
	M6DataType result = eM6NoData;
		 if (inKind == "text")		result = eM6TextData;
	else if (inKind == "string")	result = eM6StringData;
	else if (inKind == "numeric")	result = eM6NumberData;
	else if (inKind == "date")		result = eM6DateData;
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


void M6Builder::Process(const string& inDocument)
{
	M6InputDocument* doc = new M6InputDocument(*mDatabank, inDocument);
	
	const char* start = inDocument.c_str();
	const char* end = start + inDocument.length();
	boost::cmatch m;
	boost::match_flag_type flags = boost::match_default | boost::match_not_dot_newline;
	
	map<string,string> attributes;
	
	while (regex_search(start, end, m, mProcessorRE, flags))
	{
		string key(m[1].first, m[1].second);
		ba::to_lower(key);
		ba::trim(key);
		bool stop = false;
		
		foreach (M6Processor& p, mProcessors)
		{
			if (p.key != key and not p.key.empty())
				continue;

			stop = p.stop;
			
			string value(m[2].first, m[2].second);
			ba::trim(value);
			
			if (value.empty() and stop == false)
				continue;
			
			if (p.attr)
			{
				string attr = attributes[p.name];
				if (not attr.empty())
					attr += ' ';
				attributes[p.name] = attr + value;
			}
			
			if (p.index)
				doc->Index(p.name, p.type, p.unique, value);
			
			break;
		}
		
		if (stop)
			break;
		
		start = m[0].second;
		flags |= boost::match_prev_avail;
		flags |= boost::match_not_bob;
	}
	
	foreach (auto attr, attributes)
		doc->SetAttribute(attr.first, attr.second);
	
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
		Process(document);
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
					Process(document);
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

void M6Builder::SetupProcessor(zx::element* inConfig)
{
	if (inConfig == nullptr)
		THROW(("No processing information for databank in config file, cannot continue"));
	
	// fetch the processor type
		 if (inConfig->get_attribute("type") == "delimited")	mProcessorType = eM6ProcessDelimited;
	else if (inConfig->get_attribute("type") == "fixed-width")	mProcessorType = eM6ProcessFixedWidth;
	else if (inConfig->get_attribute("type") == "regex")		mProcessorType = eM6ProcessRegex;
	else THROW(("unsupported processing type"));
	
	if (mProcessorType == eM6ProcessDelimited)
	{
		string delimiter = inConfig->get_attribute("delimiter");
		boost::format fre("(.+?)%1%(.+)$");
		mProcessorRE = boost::regex((fre % delimiter).str());
	}
	
	foreach (zx::element* p, inConfig->find("process"))
	{
		string action = p->get_attribute("action");
		M6Processor proc = {
			p->get_attribute("key"),
			p->get_attribute("name"),
			MapDataType(p->get_attribute("type")),
			ba::contains(action, "attr"),
			ba::contains(action, "index"),
			ba::contains(action, "unique"),
			ba::contains(action, "stop")
		};
		
		if (proc.name.empty())
			proc.name = proc.key;
		
		foreach (zx::element* pp, p->find("postprocess"))
		{
			M6PostProcessor post = {
				boost::regex(pp->get_attribute("what")),
				pp->get_attribute("with"),
				ba::contains(pp->get_attribute("flags"), "global")
			};
			
			proc.post.push_back(post);
		}
		
		mProcessors.push_back(proc);
	}
}

void M6Builder::Build()
{
//	boost::timer::auto_cpu_timer t;

	zx::element* file = mConfig->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));

	fs::path path = file->content();
	mDatabank = M6Databank::CreateNew(path.string());
	mDatabank->StartBatchImport(mLexicon);
	
	vector<fs::path> files;
	Glob(mConfig->find_first("source"), files);

	SetupProcessor(mConfig->find_first("processing"));

	foreach (fs::path& file, files)
	{
		if (not fs::exists(file))
		{
			cerr << "file missing: " << file << endl;
			continue;
		}
		
		Parse(file);
	}
	
	cout << endl << "creating index..."; cout.flush();
	
	mDatabank->CommitBatchImport();
	cout << endl << "done" << endl;
}

