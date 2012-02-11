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
		
		M6Config::SetConfigFile(configFile);

		M6Builder builder(databank);
		builder.Build();
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
