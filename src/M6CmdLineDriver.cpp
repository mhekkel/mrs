#include "M6Lib.h"

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>

#include "M6Builder.h"
#include "M6Databank.h"
#include "M6Config.h"
#include "M6Error.h"

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

	fs::path path = file->content();
	M6Databank db(path.string(), eReadOnly);
	db.Find(vm["query"].as<string>());
}

void Info(int argc, char* argv[])
{
	cerr << "not supported yet" << endl;
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
		else if (strcmp(argv[1], "info") == 0)
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
