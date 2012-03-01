#include "M6Lib.h"

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Config.h"
#include "M6Error.h"
#include "M6Server.h"

using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;
namespace zx = zeep::xml;

int VERBOSE;

void RunMainLoop()
{
	cout << "Restarting services... "; cout.flush();
	
	vector<zeep::http::server*> servers;
	boost::thread_group threads;

	foreach (zx::element* config, M6Config::Instance().LoadServers())
	{
		string addr = config->get_attribute("addr");
		string port = config->get_attribute("port");
		if (port.empty())
			port = "80";
		
		if (VERBOSE)
			cout << "listening at " << addr << ':' << port << endl;
		
		unique_ptr<zeep::http::server> server(new M6Server(config));
		
		uint32 nrOfThreads = boost::thread::hardware_concurrency();

		server->bind(addr, boost::lexical_cast<uint16>(port));
		threads.create_thread(boost::bind(&zeep::http::server::run, server.get(), nrOfThreads));
		servers.push_back(server.release());
	}

	if (servers.empty())
	{
		cerr << "No servers configured" << endl;
		exit(1);
	}

	if (not VERBOSE)
		cout << " done" << endl;
	
	threads.join_all();

	foreach (zeep::http::server* server, servers)
		delete server;
}

int main(int argc, char* argv[])
{
	try
	{
		po::options_description desc("m6-server");
		desc.add_options()
			("config-file,c", po::value<string>(),	"Configuration file")
			("verbose,v",							"Be verbose")
			("help,h",								"Display help message")
			;
	
		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
		po::notify(vm);
	
		if (vm.count("help"))
		{
			cout << desc << "\n";
			exit(1);
		}
		
		if (vm.count("verbose"))
			VERBOSE = 1;

		fs::path configFile("config/m6-config.xml");
		if (vm.count("config-file"))
			configFile = vm["config-file"].as<string>();
		
		if (not fs::exists(configFile))
			THROW(("Configuration file not found (\"%s\")", configFile.string().c_str()));
		
		M6Config::SetConfigFile(configFile);
		
		RunMainLoop();
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
