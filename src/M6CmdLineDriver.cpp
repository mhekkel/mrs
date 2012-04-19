#include "M6Lib.h"

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/timer/timer.hpp>

#include "M6Builder.h"
#include "M6Databank.h"
#include "M6Config.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"
#include "M6Blast.h"

using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

int VERBOSE;

void Blast(int argc, char* const argv[])
{
	boost::timer::auto_cpu_timer t;

	string matrix("BLOSUM62"), program = "blastp", query;
	int32 gapOpen = -1, gapExtend = -1, wordSize = 0,
		threads = boost::thread::hardware_concurrency(), reportLimit = 250;
	bool filter = true, gapped = true;
	double expect = 10;

	po::options_description desc("m6 blast");
	desc.add_options()
		("query,i",			po::value<string>(),	"File containing query in FastA format")
		("program,p",		po::value<string>(),	"Blast program (only supported program is blastp for now...)")
		("databank,d",		po::value<string>(),	"Databank in FastA format")
		("output,o",		po::value<string>(),	"Output file, default is stdout")
		("report-limit,b",	po::value<int32>(),		"Number of results to report")
		("matrix,M",		po::value<string>(),	"Matrix (default is BLOSUM62)")
		("word-size,W",		po::value<int32>(),		"Word size (0 invokes default)")
		("gap-open,G",		po::value<int32>(),		"Cost to open a gap (-1 invokes default)")
		("gap-extend,E",	po::value<int32>(),		"Cost to extend a gap (-1 invokes default)")
		("no-filter",								"Do not mask low complexity regions in the query sequence")
		("ungapped",								"Do not search for gapped alignments, only ungapped")
		("expect,e",		po::value<double>(),	"Expectation value, default is 10.0")
		("threads,a",		po::value<int32>(),		"Nr of threads")
		("write-fasta",								"Write output as FastaA")
		("help,h",									"Display help message")
		;

	po::variables_map vm;
//		po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
	po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
	po::notify(vm);

	if (vm.count("help") or vm.count("databank") == 0 or vm.count("query") == 0)
	{
		cout << desc << "\n";
		exit(1);
	}
	
	fs::path queryFile(vm["query"].as<string>());
	if (not fs::exists(queryFile))
		throw M6Exception("Query file does not exist");
	fs::ifstream queryData(queryFile);
	if (not queryData.is_open())
		throw M6Exception("Could not open query file");

	for (;;)
	{
		string line;
		getline(queryData, line);
		if (line.empty() and queryData.eof())
			break;
		query += line + '\n';
	}
	
	fs::path databank(vm["databank"].as<string>());
	if (not fs::exists(databank))
		throw M6Exception("Databank does not exist");
	
	if (vm.count("program"))		program = vm["program"].as<string>();
	if (vm.count("matrix"))			matrix = vm["matrix"].as<string>();
	if (vm.count("report-limit"))	reportLimit = vm["report-limit"].as<int32>();
	if (vm.count("word-size"))		wordSize = vm["word-size"].as<int32>();
	if (vm.count("gap-open"))		gapOpen = vm["gap-open"].as<int32>();
	if (vm.count("gap-extend"))		gapExtend = vm["gap-extend"].as<int32>();
	if (vm.count("no-filter"))		filter = false;
	if (vm.count("ungapped"))		gapped = false;
	if (vm.count("expect"))			expect = vm["expect"].as<double>();
	if (vm.count("threads"))		threads = vm["threads"].as<int32>();

	if (vm.count("write-fasta"))
	{
		if (vm.count("output") and vm["output"].as<string>() != "stdout")
		{
			fs::ofstream out(vm["output"].as<string>());
			M6Blast::SearchAndWriteResultsAsFastA(out, databank, query, program, matrix,
				wordSize, expect, filter, gapped, gapOpen, gapExtend, reportLimit, threads);
		}
		else
			M6Blast::SearchAndWriteResultsAsFastA(cout, databank, query, program, matrix,
				wordSize, expect, filter, gapped, gapOpen, gapExtend, reportLimit, threads);
	}
	else
	{
		M6Blast::Result* r = M6Blast::Search(databank, query, program, matrix,
			wordSize, expect, filter, gapped, gapOpen, gapExtend, reportLimit, threads);
	
		if (vm.count("output") and vm["output"].as<string>() != "stdout")
		{
			fs::ofstream out(vm["output"].as<string>());
			out << *r;
		}
		else
			cout << *r << endl;
	
		delete r;
	}
}

void Build(int argc, char* argv[])
{
	po::options_description desc("m6 build");
	desc.add_options()
		("databank,d",	po::value<string>(),	"Databank to build")
		("config-file,c", po::value<string>(),	"Configuration file")
		("verbose,v",							"Be verbose")
		("threads,a", po::value<uint32>(),		"Nr of threads/pipelines")
		("help,h",								"Display help message")
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
	
	M6Config::SetConfigFile(configFile);

	uint32 nrOfThreads = boost::thread::hardware_concurrency();
	if (nrOfThreads > 4)
		nrOfThreads = 4;
	if (vm.count("threads"))
		nrOfThreads = vm["threads"].as<uint32>();
	if (nrOfThreads < 1)
		nrOfThreads = 1;

	M6Builder builder(databank);
	builder.Build(nrOfThreads);
}

void Query(int argc, char* argv[])
{
	po::options_description desc("m6 query");
	desc.add_options()
		("databank,d",	po::value<string>(),	"Databank to build")
		("query,q", po::value<string>(),		"Query term")
		("config-file,c", po::value<string>(),	"Configuration file")
		("count", po::value<uint32>(),			"Result count (default = 10)")
		("offset", po::value<uint32>(),			"Result offset (default = 0)")
		("verbose,v",							"Be verbose")
		("help,h",								"Display help message")
		;

	po::positional_options_description p;
	p.add("databank", 1);
	p.add("query", 2);
	
	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
	po::notify(vm);

	if (vm.count("help") or vm.count("databank") == 0 or vm.count("query") == 0)
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
	
	M6Config::SetConfigFile(configFile);

	zeep::xml::element* config = M6Config::Instance().LoadDatabank(databank);
	if (not config)
		THROW(("Configuration for %s is missing", databank.c_str()));

	zeep::xml::element* file = config->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));

	uint32 count = 10;
	if (vm.count("count"))
		count = vm["count"].as<uint32>();
	uint32 offset = 0;
	if (vm.count("offset"))
		offset = vm["offset"].as<uint32>();

	fs::path path = file->content();
	M6Databank db(path.string(), eReadOnly);
	unique_ptr<M6Iterator> rset(db.Find(vm["query"].as<string>(), true, offset + count));
	
	if (rset)
	{
		// print results
		uint32 docNr;
		float rank;

		while (offset-- > 0 and rset->Next(docNr, rank))
			;
		
		while (rset->Next(docNr, rank))
		{
			unique_ptr<M6Document> doc(db.Fetch(docNr));
			
			cout << doc->GetAttribute("id") << "\t"
				 << boost::format("%1.2f") % rank << "\t"
				 << doc->GetAttribute("title") << endl;
		}
	}
}

void Info(int argc, char* argv[])
{
	po::options_description desc("m6 info");
	desc.add_options()
		("databank,d",	po::value<string>(),	"Databank to build")
		("config-file,c", po::value<string>(),	"Configuration file")
		("verbose,v",							"Be verbose")
		("help,h",								"Display help message")
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
	
	M6Config::SetConfigFile(configFile);

	zeep::xml::element* config = M6Config::Instance().LoadDatabank(databank);
	if (not config)
		THROW(("Configuration for %s is missing", databank.c_str()));

	zeep::xml::element* file = config->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));

	fs::path path = file->content();
	M6Databank db(path.string(), eReadOnly);
	
	M6DatabankInfo info;
	db.GetInfo(info);
	
	auto formatNr = [](int64 nr, int width) -> string
	{
		string result;
		int digits = 0;
		
		if (nr == 0)
			result = "0";
		
		while (nr > 0)
		{
			result += ('0' + nr % 10);
			nr /= 10;
			if (nr > 0 and ++digits % 3 == 0)
				result += '.';
		}

		if (result.length() < width)
			result.append(width - result.length(), ' ');

		reverse(result.begin(), result.end());
		
		return result;
	};

	auto descIxType = [](M6IndexType inType) -> const char*
	{
		const char* desc;
		switch (inType)
		{
			case eM6CharIndex:			desc = "unique string     "; break;
			case eM6NumberIndex:		desc = "unique number     "; break;
			case eM6CharMultiIndex:		desc = "string            "; break;
			case eM6NumberMultiIndex:	desc = "number            "; break;
			case eM6CharMultiIDLIndex:	desc = "word with position"; break;
			case eM6CharWeightedIndex:	desc = "weighted word     "; break;
		}
		return desc;
	};
	
	cout << "Statistics for databank " << path << endl
		 << endl
		 << "Number of documents : " << formatNr(info.mDocCount, 18) << endl
		 << "Raw text in bytes   : " << formatNr(info.mRawTextSize, 18) << endl
		 << "Data store size     : " << formatNr(info.mDataStoreSize, 18) << endl
		 << endl
		 << "Index Name           |                    | Nr of keys   | File size" << endl
		 << "-------------------------------------------------------------------------" << endl;
	
	foreach (M6IndexInfo& ix, info.mIndexInfo)
		cout << ix.mName << string(20 - ix.mName.length(), ' ') << " | "
			 << descIxType(ix.mType) << " | "
			 << formatNr(ix.mCount, 12) << " | "
			 << formatNr(ix.mFileSize, 14) << endl;
}

void Dump(int argc, char* argv[])
{
	po::options_description desc("m6 dump");
	desc.add_options()
		("databank,d",	po::value<string>(),	"Databank to build")
		("config-file,c", po::value<string>(),	"Configuration file")
		("index,i", po::value<string>(),		"Index to dump")
		("verbose,v",							"Be verbose")
		("help,h",								"Display help message")
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
	
	M6Config::SetConfigFile(configFile);

	zeep::xml::element* config = M6Config::Instance().LoadDatabank(databank);
	if (not config)
		THROW(("Configuration for %s is missing", databank.c_str()));

	zeep::xml::element* file = config->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));

	fs::path path = file->content();
	M6Databank db(path.string(), eReadOnly);
	
	if (vm.count("index"))
	{
		db.DumpIndex(vm["index"].as<string>(), cout);
	}
	
}

void Vacuum(int argc, char* argv[])
{
	po::options_description desc("m6 vacuum");
	desc.add_options()
		("databank,d",	po::value<string>(),	"Databank to build")
		("config-file,c", po::value<string>(),	"Configuration file")
		("verbose,v",							"Be verbose")
		("help,h",								"Display help message")
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
	
	M6Config::SetConfigFile(configFile);

	zeep::xml::element* config = M6Config::Instance().LoadDatabank(databank);
	if (not config)
		THROW(("Configuration for %s is missing", databank.c_str()));

	zeep::xml::element* file = config->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));

	fs::path path = file->content();
	M6Databank db(path.string(), eReadWrite);
	db.Vacuum();
}

void Validate(int argc, char* argv[])
{
	po::options_description desc("m6 validate");
	desc.add_options()
		("databank,d",	po::value<string>(),	"Databank to build")
		("config-file,c", po::value<string>(),	"Configuration file")
		("verbose,v",							"Be verbose")
		("help,h",								"Display help message")
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
	
	M6Config::SetConfigFile(configFile);

	zeep::xml::element* config = M6Config::Instance().LoadDatabank(databank);
	if (not config)
		THROW(("Configuration for %s is missing", databank.c_str()));

	zeep::xml::element* file = config->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));

	fs::path path = file->content();
	M6Databank db(path.string(), eReadOnly);
	db.Validate();
}

int main(int argc, char* argv[])
{
	try
	{
		if (argc < 2)
		{
			cout << "Usage: m6 command [options]" << endl
				 << endl
				 << "  Command can be one of: build, query, info" << endl
				 << "  Use m6 command --help for more info on each command" << endl;
			exit(1);
		}
		
		if (strcmp(argv[1], "blast") == 0)
			Blast(argc - 1, argv + 1);
		else if (strcmp(argv[1], "build") == 0)
			Build(argc - 1, argv + 1);
		else if (strcmp(argv[1], "query") == 0)
			Query(argc - 1, argv + 1);
		else if (strcmp(argv[1], "info") == 0)
			Info(argc - 1, argv + 1);
		else if (strcmp(argv[1], "dump") == 0)
			Dump(argc - 1, argv + 1);
		else if (strcmp(argv[1], "vacuum") == 0)
			Vacuum(argc - 1, argv + 1);
		else if (strcmp(argv[1], "validate") == 0)
			Validate(argc - 1, argv + 1);
		else
		{
			cout << "Unknown command " << argv[1] << endl
				 << "Supported commands are build, query and info" << endl;
			exit(1);
		}
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
