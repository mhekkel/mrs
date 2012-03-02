#include "M6Lib.h"

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>

#include "M6Builder.h"
#include "M6Databank.h"
#include "M6Config.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"

using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

int VERBOSE;

void Build(int argc, char* argv[])
{
	po::options_description desc("m6 build");
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

	M6Builder builder(databank);
	builder.Build();
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

	zeep::xml::element* config = M6Config::Instance().LoadConfig(databank);
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

	zeep::xml::element* config = M6Config::Instance().LoadConfig(databank);
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

	zeep::xml::element* config = M6Config::Instance().LoadConfig(databank);
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

	zeep::xml::element* config = M6Config::Instance().LoadConfig(databank);
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
		
		if (strcmp(argv[1], "build") == 0)
			Build(argc - 1, argv + 1);
		else if (strcmp(argv[1], "query") == 0)
			Query(argc - 1, argv + 1);
		else if (strcmp(argv[1], "info") == 0 or strcmp(argv[1], "dump") == 0)
			Info(argc - 1, argv + 1);
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
