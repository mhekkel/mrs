#include "M6Lib.h"

#include <iostream>
#include <memory>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/tr1/tuple.hpp>
#include <boost/timer/timer.hpp>

#include <zeep/xml/document.hpp>

#include "M6DocStore.h"
#include "M6Error.h"
#include "M6Databank.h"
#include "M6Document.h"

using namespace std;
using namespace std::tr1;
namespace zx = zeep::xml;
namespace po = boost::program_options;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

int VERBOSE = 0;

void Glob(zx::element* inSource, vector<fs::path>& outFiles)
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

struct M6AttributeParser
{
	string			name;
	boost::regex	re;
	string			repeat;
	bool			index;
};

typedef vector<M6AttributeParser> M6AttributeParsers;

void Store(M6Databank& inDatabank, const string& inDocument,
	M6AttributeParsers& inAttributes)
{
	M6InputDocument* doc = new M6InputDocument(inDatabank, inDocument);
	
	foreach (M6AttributeParser& p, inAttributes)
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
		
		if (not value.empty())
			doc->SetAttribute(p.name, value);
	}
	
	inDatabank.Store(doc);

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

void Parse(M6Databank& inDatabank, zx::element* inConfig,
	M6AttributeParsers& inAttributes, const fs::path& inFile)
{
	fs::ifstream file(inFile, ios::binary);
	string line;
	
	// do we need to strip off a header?
	zx::element* header = inConfig->find_first("header-line");
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

	zx::element* separator = inConfig->find_first("document-separator");
	if (separator == nullptr)	// one file per document
	{
		for (;;)
		{
			document += line + "\n";
			getline(file, line);
			if (line.empty() and file.eof())
				break;
		}
		Store(inDatabank, document, inAttributes);
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
					Store(inDatabank, document, inAttributes);
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

void Build(zx::element* inConfig)
{
	boost::timer::auto_cpu_timer t;

	zx::element* file = inConfig->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));
	unique_ptr<M6Databank> databank(M6Databank::CreateNew(file->str()));
	
	vector<fs::path> files;
	Glob(inConfig->find_first("source"), files);

	// prepare the attribute parsers
	M6AttributeParsers attributes;
	foreach (zx::element* attr, inConfig->find("document-attributes/document-attribute"))
	{
		M6AttributeParser p = {
			attr->get_attribute("name"),
			boost::regex(attr->get_attribute("match")),
			attr->get_attribute("repeat"),
			attr->get_attribute("index") == "true"
		};
		
		if (p.name.empty() or p.re.empty())
			continue;
		
		attributes.push_back(p);
	}
	
	foreach (fs::path& file, files)
	{
		if (not fs::exists(file))
		{
			cerr << "file missing: " << file << endl;
			continue;
		}
		
		Parse(*databank, inConfig, attributes, file);
	}
	
	databank->Commit();
	cout << endl << "done" << endl;
}

int main(int argc, char* argv[])
{
	try
	{
		po::options_description desc("m6-build options");
		desc.add_options()
			("help,h",								"Display help message")
			("databank,d",	po::value<string>(),	"Databank to build")
			("config-file,c", po::value<string>(),	"Configuration file")
			("verbose,v",							"Be verbose")
			;

		po::positional_options_description p;
		p.add("databank", 1);
		
		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
		po::notify(vm);

		if (vm.count("help") or vm.count("databank") == 0)
		{
			cout << desc << "\n";
			exit(1);
		}
		
		if (vm.count("verbose"))
			VERBOSE = 1;
		
		string databank = vm["databank"].as<string>();

		fs::path configFile("config/m6-config.xml");
		if (vm.count("config-file"))
			configFile = vm["config-file"].as<string>();
		
		if (not fs::exists(configFile))
			THROW(("Configuration file not found (\"%s\")", configFile.string().c_str()));
		
		fs::ifstream configFileStream(configFile, ios::binary);
		zx::document config(configFileStream);
		
		string dbConfigPath = (boost::format("/m6-config/databank[@id='%1%']") % databank).str();
		auto dbConfig = config.find(dbConfigPath);
		if (dbConfig.empty())
			THROW(("databank %s not specified in config file", databank.c_str()));
		
		if (dbConfig.size() > 1)
			THROW(("databank %s specified multiple times in config file", databank.c_str()));

		Build(dbConfig.front());

	}
	catch (exception& e)
	{
		cerr << endl
			 << "m6-builder exited with an exception:" << endl
			 << e.what() << endl;
		exit(1);
	}
	catch (...)
	{
		cerr << endl
			 << "m6-builder exited with an uncaught exception" << endl;
		exit(1);
	}
	
	return 0;
}
