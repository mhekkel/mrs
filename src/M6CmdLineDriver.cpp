#include "M6Lib.h"

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>

#include "M6Builder.h"
#include "M6Config.h"
#include "M6Error.h"

using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

int VERBOSE;

void Dump(const string& inDatabank, int inLevel)
{
	
}

int main(int argc, char* argv[])
{
	try
	{
		po::options_description desc("m6-build options");
		desc.add_options()
			("help,h",								"Display help message")
			("action", po::value<string>(),			"Action to perform [build,dump,query]")
			("databank,d",	po::value<string>(),	"Databank to build")
			("config-file,c", po::value<string>(),	"Configuration file")
			("level", po::value<int>(),				"Dump level, the higher the more information")
			("verbose,v",							"Be verbose")
			;

		po::positional_options_description p;
		p.add("action", 1);
		p.add("databank", 2);
		
		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
		po::notify(vm);

		if (vm.count("help") or vm.count("action") == 0 or vm.count("databank") == 0)
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

		if (vm["action"].as<string>() == "build")
		{
			M6Builder builder(databank);
			builder.Build();
		}
		else if (vm["action"].as<string>() == "dump")
		{
			int level = 0;
			if (vm.count("level"))
				level = vm["level"].as<int>();
			Dump(databank, level);
		}
		else
			THROW(("unimplemented action '%s'", vm["action"].as<string>().c_str()));
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
