#include "M6Lib.h"

#include <signal.h>

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
//#include <boost/timer/timer.hpp>
#include <boost/tr1/tuple.hpp>
#include <boost/algorithm/string.hpp>

#include "M6Builder.h"
#include "M6Databank.h"
#include "M6Config.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"
#include "M6Blast.h"
#include "M6Progress.h"
#include "M6Fetch.h"
#include "M6MD5.h"
#include "M6Utilities.h"
#include "M6Server.h"

#if defined _MSC_VER
#define WIN32_LEAN_AND_MEAN   
#include <windows.h>
#endif

using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;
namespace zx = zeep::xml;
namespace ba = boost::algorithm;

int VERBOSE;

// --------------------------------------------------------------------
// abstract base class driver

class M6CmdLineDriver
{
  public:
	virtual			~M6CmdLineDriver() {}

	static void		Exec(int argc, char* const argv[]);

	static void		Terminated();
	static void		SigHandler(int inSignal);
#if defined _MSC_VER
	static BOOL WINAPI
					ConsoleHandler(DWORD inCEvent);
#endif

  protected:

					M6CmdLineDriver();

	virtual void	AddOptions(po::options_description& desc,
						unique_ptr<po::positional_options_description>& p);
	virtual bool	Validate(po::variables_map& vm);
	virtual void	Exec(const string& inCommand, po::variables_map& vm) = 0;

	tr1::tuple<const zx::element*,fs::path>
					GetDatabank(const string& inDatabank);

	static string	sDatabank;
};

// --------------------------------------------------------------------
// implementations

class M6BlastDriver : public M6CmdLineDriver
{
  public:
					M6BlastDriver() {};

	virtual void	AddOptions(po::options_description& desc,
						unique_ptr<po::positional_options_description>& p);
	virtual bool	Validate(po::variables_map& vm);
	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6BuildDriver : public M6CmdLineDriver
{
  public:
					M6BuildDriver() {};

	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6QueryDriver : public M6CmdLineDriver
{
  public:
					M6QueryDriver() {};

	virtual void	AddOptions(po::options_description& desc,
						unique_ptr<po::positional_options_description>& p);
	virtual bool	Validate(po::variables_map& vm);
	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6InfoDriver : public M6CmdLineDriver
{
  public:
					M6InfoDriver() {};

	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6EntryDriver : public M6CmdLineDriver
{
  public:
					M6EntryDriver() {};

	virtual void	AddOptions(po::options_description& desc,
						unique_ptr<po::positional_options_description>& p);
	virtual bool	Validate(po::variables_map& vm);
	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6DumpDriver : public M6CmdLineDriver
{
  public:
					M6DumpDriver() {};

	virtual void	AddOptions(po::options_description& desc,
						unique_ptr<po::positional_options_description>& p);
	virtual bool	Validate(po::variables_map& vm);
	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6FetchDriver : public M6CmdLineDriver
{
  public:
					M6FetchDriver() {};

	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6VacuumDriver : public M6CmdLineDriver
{
  public:
					M6VacuumDriver() {};

	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6ValidateDriver : public M6CmdLineDriver
{
  public:
					M6ValidateDriver() {};

	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6PasswordDriver : public M6CmdLineDriver
{
  public:
					M6PasswordDriver() {};

	virtual void	AddOptions(po::options_description& desc,
						unique_ptr<po::positional_options_description>& p);
	virtual bool	Validate(po::variables_map& vm);
	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

class M6ServerDriver : public M6CmdLineDriver
{
  public:
					M6ServerDriver() {};

	virtual void	AddOptions(po::options_description& desc,
						unique_ptr<po::positional_options_description>& p);
	virtual bool	Validate(po::variables_map& vm);
	virtual void	Exec(const string& inCommand, po::variables_map& vm);
};

// --------------------------------------------------------------------
// Code for base class driver

string M6CmdLineDriver::sDatabank;

M6CmdLineDriver::M6CmdLineDriver()
{
}

void M6CmdLineDriver::Exec(int argc, char* const argv[])
{
	unique_ptr<M6CmdLineDriver> driver;
	
	if (strcmp(argv[1], "blast") == 0)
		driver.reset(new M6BlastDriver());
	else if (strcmp(argv[1], "build") == 0 or strcmp(argv[1], "update") == 0)
		driver.reset(new M6BuildDriver());
	else if (strcmp(argv[1], "query") == 0)
		driver.reset(new M6QueryDriver());
	else if (strcmp(argv[1], "info") == 0)
		driver.reset(new M6InfoDriver());
	else if (strcmp(argv[1], "entry") == 0)
		driver.reset(new M6EntryDriver());
	else if (strcmp(argv[1], "dump") == 0)
		driver.reset(new M6DumpDriver());
	else if (strcmp(argv[1], "fetch") == 0)
		driver.reset(new M6FetchDriver());
	else if (strcmp(argv[1], "vacuum") == 0)
		driver.reset(new M6VacuumDriver());
	else if (strcmp(argv[1], "validate") == 0)
		driver.reset(new M6ValidateDriver());
	else if (strcmp(argv[1], "password") == 0)
		driver.reset(new M6PasswordDriver());
	else if (strcmp(argv[1], "server") == 0)
		driver.reset(new M6ServerDriver());
	else
	{
		cout << "Unknown command " << argv[1] << endl
			 << "Supported commands are build, query and info" << endl;
		exit(1);
	}

	po::options_description desc(string("m6 ") + argv[1]);
	po::variables_map vm;
	unique_ptr<po::positional_options_description> p;

	driver->AddOptions(desc, p);
	
	if (p)
		po::store(po::command_line_parser(argc - 1, argv + 1).options(desc).positional(*p).run(), vm);
	else
		po::store(po::command_line_parser(argc - 1, argv + 1).options(desc).run(), vm);
	
	po::notify(vm);

	if (not driver->Validate(vm))
	{
		cout << desc << "\n";
		exit(1);
	}		
	
	if (vm.count("verbose"))
		VERBOSE = 1;
	
	driver->Exec(argv[1], vm);
}

void M6CmdLineDriver::AddOptions(po::options_description& desc,
	unique_ptr<po::positional_options_description>& p)
{
	desc.add_options()	
		("databank,d",	po::value<string>(),	"Databank to build")
		("config-file,c", po::value<string>(),	"Configuration file")
		("verbose,v",							"Be verbose")
		("threads,a", po::value<uint32>(),		"Nr of threads/pipelines")
		("help,h",								"Display help message")
		;

	p.reset(new po::positional_options_description());
	p->add("databank", 1);
}

bool M6CmdLineDriver::Validate(po::variables_map& vm)
{
	bool result = false;
	
	if (vm.count("help") == 0 and vm.count("databank") > 0)
	{
		sDatabank = vm["databank"].as<string>();
		
		fs::path configFile("config/m6-config.xml");
		if (vm.count("config-file"))
			configFile = vm["config-file"].as<string>();
		
		if (not fs::exists(configFile))
			THROW(("Configuration file not found (\"%s\")", configFile.string().c_str()));
		
		M6Config::SetConfigFilePath(configFile);

		result = true;
	}
	
	return result;
}

tr1::tuple<const zx::element*,fs::path>
M6CmdLineDriver::GetDatabank(const string& inDatabank)
{
	const zx::element* config = M6Config::GetEnabledDatabank(inDatabank);

	fs::path path = M6Config::GetDbDirectory(inDatabank);
	
	return tr1::make_tuple(config, path);
}

void M6CmdLineDriver::Terminated()
{
	M6Status::Instance().SetError(sDatabank, M6Exception::last_what());
	cerr << "Terminated" << endl
		 << M6Exception::last_what() << endl;
	abort();
}

void M6CmdLineDriver::SigHandler(int inSignal)
{
	char msg[256];
	sprintf(msg, "terminated on signal %d", inSignal);
	
	cerr << "Terminated" << endl
		 << msg << endl;

	M6Status::Instance().SetError(sDatabank, msg);
	exit(1);
}

#if defined _MSC_VER

BOOL WINAPI M6CmdLineDriver::ConsoleHandler(DWORD CEvent)
{
    char msg[128];

    switch(CEvent)
    {
	    case CTRL_C_EVENT:			sprintf(msg, "terminated on CTRL-C"); break;
	    case CTRL_BREAK_EVENT:		sprintf(msg, "terminated on CTRL-BREAK"); break;
	    case CTRL_CLOSE_EVENT:		sprintf(msg, "terminated on close event"); break;
	    case CTRL_LOGOFF_EVENT:		sprintf(msg, "terminated on logging off"); break;
	    case CTRL_SHUTDOWN_EVENT:	sprintf(msg, "terminated on shutdown"); break;
	    default:					sprintf(msg, "terminated on unknown event"); break;
    }

	M6Status::Instance().SetError(sDatabank, msg);

	cerr << "Terminated" << endl
		 << msg << endl;

	abort();

    return TRUE;
}

#endif

// --------------------------------------------------------------------
//	blast

void M6BlastDriver::AddOptions(po::options_description& desc,
	unique_ptr<po::positional_options_description>& p)
{
	desc.add_options()
		("query,i",			po::value<string>(),	"File containing query in FastA format")
		("program,p",		po::value<string>(),	"Blast program (only supported program is blastp for now...)")
		("databank,d",		po::value<string>(),	"Databank(s) in FastA format, can be specified multiple times")
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
		//("write-fasta",								"Write output as FastaA")
		("verbose,v",								"Be verbose")
		("config-file,c",	po::value<string>(),	"Configuration file")
		("help,h",									"Display help message")
		;
}

bool M6BlastDriver::Validate(po::variables_map& vm)
{
	bool result = false;
	
	if (vm.count("help") == 0 and vm.count("databank") > 0 and vm.count("query") > 0)
	{
		fs::path configFile("config/m6-config.xml");
		if (vm.count("config-file"))
			configFile = vm["config-file"].as<string>();
		
		if (not fs::exists(configFile))
			THROW(("Configuration file not found (\"%s\")", configFile.string().c_str()));
		
		M6Config::SetConfigFilePath(configFile);

		result = true;
	}
	
	return result;
}

void M6BlastDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	string matrix("BLOSUM62"), program = "blastp", query;
	int32 gapOpen = -1, gapExtend = -1, wordSize = 0,
		threads = boost::thread::hardware_concurrency(), reportLimit = 250;
	bool filter = true, gapped = true;
	double expect = 10;

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

	vector<fs::path> databanks;
	string db = vm["databank"].as<string>();
	foreach (const zx::element* dbc, M6Config::GetDatabanks(db))
	{
		fs::path dbdir = M6Config::GetDbDirectory(dbc->get_attribute("id"));

		if (not fs::exists(dbdir / "fasta"))
			THROW(("Databank '%s' does not contain a fasta file", db.c_str()));
		
		databanks.push_back(dbdir / "fasta");
	}

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

	//if (vm.count("write-fasta"))
	//{
	//	if (vm.count("output") and vm["output"].as<string>() != "stdout")
	//	{
	//		fs::ofstream out(vm["output"].as<string>());
	//		M6Blast::SearchAndWriteResultsAsFastA(out, databanks, query, program, matrix,
	//			wordSize, expect, filter, gapped, gapOpen, gapExtend, reportLimit, threads);
	//	}
	//	else
	//		M6Blast::SearchAndWriteResultsAsFastA(cout, databanks, query, program, matrix,
	//			wordSize, expect, filter, gapped, gapOpen, gapExtend, reportLimit, threads);
	//}
	//else
	{
		M6Blast::Result* r = M6Blast::Search(databanks, query, program, matrix,
			wordSize, expect, filter, gapped, gapOpen, gapExtend, reportLimit, threads);
			
		r->mDb = db;
	
		if (vm.count("output") and vm["output"].as<string>() != "stdout")
		{
			fs::ofstream out(vm["output"].as<string>());
			r->WriteAsNCBIBlastXML(out);
		}
		else
			r->WriteAsNCBIBlastXML(cout);
	
		delete r;
	}
}

// --------------------------------------------------------------------
//	build

void M6BuildDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	uint32 nrOfThreads = boost::thread::hardware_concurrency();
	if (nrOfThreads > 4)
		nrOfThreads = 4;
	if (vm.count("threads"))
		nrOfThreads = vm["threads"].as<uint32>();
	if (nrOfThreads < 1)
		nrOfThreads = 1;
	
	vector<string> databanks;
	if (sDatabank == "all" or sDatabank == "daily" or sDatabank == "weekly" or sDatabank == "monthly")
	{
		foreach (zx::element* db, M6Config::GetDatabanks())
		{
			if (db->get_attribute("enabled") != "true")
				continue;
			
			if (sDatabank == "all" or db->get_attribute("update") == sDatabank)
				databanks.push_back(db->get_attribute("id"));
		}
	}
	else
		databanks.push_back(sDatabank);

	foreach (string databank, databanks)
	{
		cout << inCommand << " " << databank << endl;
		
		if (inCommand == "update" and M6Config::GetDatabankParam(databank, "source/@fetch").empty() == false)
			M6Fetch(databank);
	
		M6Builder builder(databank);
		
		if (inCommand == "build" or builder.NeedsUpdate())
		{
			try
			{
				builder.Build(nrOfThreads);
			}
			catch (exception& e)
			{
				cerr << endl
					 << "Error" << endl
					 << e.what() << endl;
				M6Status::Instance().SetError(databank, e.what());
			}
		}
		else
		{
			cout << databank << " is up-to-date" << endl;
			M6Status::Instance().Cleanup(databank);
		}
		
		cout << endl;
	}
}

// --------------------------------------------------------------------
//	query

void M6QueryDriver::AddOptions(po::options_description& desc,
	unique_ptr<po::positional_options_description>& p)
{
	M6CmdLineDriver::AddOptions(desc, p);
	desc.add_options()
		("query,q", po::value<string>(),		"Query term")
		("count", po::value<uint32>(),			"Result count (default = 10)")
		("offset", po::value<uint32>(),			"Result offset (default = 0)")
		;

	p->add("query", 2);
}

bool M6QueryDriver::Validate(po::variables_map& vm)
{
	return M6CmdLineDriver::Validate(vm) and vm.count("query");
}

void M6QueryDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	const zx::element* config;
	fs::path path;
	tr1::tie(config, path) = GetDatabank(vm["databank"].as<string>());

	M6Databank db(path.string());

	uint32 count = 10;
	if (vm.count("count"))
		count = vm["count"].as<uint32>();
	uint32 offset = 0;
	if (vm.count("offset"))
		offset = vm["offset"].as<uint32>();

	unique_ptr<M6Iterator> rset(db.Find(vm["query"].as<string>(), true, offset + count));
	
	if (rset)
	{
		// print results
		uint32 docNr;
		float rank;

		while (offset-- > 0 and rset->Next(docNr, rank))
			;
		
		while (count-- > 0 and rset->Next(docNr, rank))
		{
			unique_ptr<M6Document> doc(db.Fetch(docNr));
			
			cout << doc->GetAttribute("id") << "\t"
				 << boost::format("%1.2f") % (100.0 * rank) << "\t"
				 << doc->GetAttribute("title") << endl;
		}
	}
}

// --------------------------------------------------------------------
//	info

void M6InfoDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	const zx::element* config;
	fs::path path;
	tr1::tie(config, path) = GetDatabank(vm["databank"].as<string>());

	M6Databank db(path.string());
	
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

// --------------------------------------------------------------------
//	dump

void M6DumpDriver::AddOptions(po::options_description& desc,
	unique_ptr<po::positional_options_description>& p)
{
	desc.add_options()
		("index,i", po::value<string>(),		"Index to dump")
		;

	M6CmdLineDriver::AddOptions(desc, p);

	p->add("query", 2);
}

bool M6DumpDriver::Validate(po::variables_map& vm)
{
	return M6CmdLineDriver::Validate(vm) and vm.count("index");
}

void M6DumpDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	const zx::element* config;
	fs::path path;
	tr1::tie(config, path) = GetDatabank(vm["databank"].as<string>());

	M6Databank db(path.string());
	
	db.DumpIndex(vm["index"].as<string>(), cout);
}

// --------------------------------------------------------------------
//	entry

void M6EntryDriver::AddOptions(po::options_description& desc,
	unique_ptr<po::positional_options_description>& p)
{
	desc.add_options()
		("entry,e", po::value<string>(),		"Entry ID to display")
		;

	M6CmdLineDriver::AddOptions(desc, p);

	p->add("entry", 2);
}

bool M6EntryDriver::Validate(po::variables_map& vm)
{
	return M6CmdLineDriver::Validate(vm) and vm.count("entry");
}

void M6EntryDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	const zx::element* config;
	fs::path path;
	tr1::tie(config, path) = GetDatabank(vm["databank"].as<string>());

	M6Databank db(path.string());
	
	unique_ptr<M6Iterator> iter(db.Find("id", vm["entry"].as<string>()));
	uint32 docNr;
	float rank;

	if (not iter or not iter->Next(docNr, rank))
		THROW(("Entry not found"));

	unique_ptr<M6Document> doc(db.Fetch(docNr));
	if (not doc)
		THROW(("Failed to fetch document???"));
	
	cout << doc->GetText() << endl;
}

// --------------------------------------------------------------------
//	fetch

void M6FetchDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	M6Fetch(vm["databank"].as<string>());
}

// --------------------------------------------------------------------
//	vacuum

void M6VacuumDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	const zx::element* config;
	fs::path path;
	tr1::tie(config, path) = GetDatabank(vm["databank"].as<string>());

	M6Databank db(path.string(), eReadWrite);

	db.Vacuum();
}

// --------------------------------------------------------------------
//	validate

void M6ValidateDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	const zx::element* config;
	fs::path path;
	tr1::tie(config, path) = GetDatabank(vm["databank"].as<string>());

	M6Databank db(path.string());

	db.Validate();
}

// --------------------------------------------------------------------
//	password

void M6PasswordDriver::AddOptions(po::options_description& desc,
	unique_ptr<po::positional_options_description>& p)
{
	desc.add_options()
		("config-file,c", po::value<string>(),	"Configuration file")
		("user,u", po::value<string>(),			"User to modify")
		("realm,r", po::value<string>(),		"Realm to modify (default is 'M6 Administrator'")
		;

	p.reset(new po::positional_options_description());
	p->add("user", 1);
	p->add("realm", 2);
}

bool M6PasswordDriver::Validate(po::variables_map& vm)
{
	fs::path configFile("config/m6-config.xml");
	if (vm.count("config-file"))
		configFile = vm["config-file"].as<string>();
		
	if (not fs::exists(configFile))
		THROW(("Configuration file not found (\"%s\")", configFile.string().c_str()));
		
	M6Config::SetConfigFilePath(configFile);

	return true;
}

void M6PasswordDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	string username, realm = "M6 Administrator", password;

	if (vm.count("user"))
		username = vm["user"].as<string>();
	
	if (username.empty())
	{
		cout << "Enter username: "; cout.flush();
		getline(cin, username);
	}
	
	if (vm.count("realm"))
		realm = vm["realm"].as<string>();
	
	if (realm.empty())
	{
		cout << "Enter realm:    "; cout.flush();
		getline(cin, realm);
	}
	
	cout << "Enter password: "; cout.flush(); SetStdinEcho(false);
	getline(cin, password);
	SetStdinEcho(true);

	string hash = M6MD5(username + ':' + realm + ':' + password).Finalise();

	M6Config::File& config(M6Config::File::Instance());
	zx::element* user = config.GetUser(username, realm);
	user->set_attribute("password", hash);
	config.WriteOut();
//
//	M6Config::Instance().SetPassword(realm, username, hash);
}

// --------------------------------------------------------------------
//	Server

void M6ServerDriver::AddOptions(po::options_description& desc,
						unique_ptr<po::positional_options_description>& p)
{
	desc.add_options()
		("config-file,c", po::value<string>(),	"Configuration file")
		("command", po::value<string>(),		"Command, one of start, restart, status or stop") 
		;

	p.reset(new po::positional_options_description());
	p->add("command", 1);
}

bool M6ServerDriver::Validate(po::variables_map& vm)
{
	fs::path configFile("config/m6-config.xml");
	if (vm.count("config-file"))
		configFile = vm["config-file"].as<string>();
		
	if (not fs::exists(configFile))
		THROW(("Configuration file not found (\"%s\")", configFile.string().c_str()));
		
	M6Config::SetConfigFilePath(configFile);
	
	return vm.count("command") > 0;
}

void M6ServerDriver::Exec(const string& inCommand, po::variables_map& vm)
{
	string command = vm["command"].as<string>();
	
	if (command == "start")
		M6Server::Start();
	else if (command == "restart")
	{
		M6Server::Stop();
		M6Server::Start();
	}
	else if (command == "stop")
		M6Server::Stop();
	//else if (command == "status")
	//	M6Server::Status();
	else
		THROW(("Invalid command '%s'", command.c_str()));
}

// --------------------------------------------------------------------
//	main

int main(int argc, char* argv[])
{
	try
	{
		set_terminate(&M6CmdLineDriver::Terminated);
	
#if defined _MSC_VER
		if (::SetConsoleCtrlHandler((PHANDLER_ROUTINE)&M6CmdLineDriver::ConsoleHandler, TRUE) == FALSE)
		{
			// unable to install handler... 
			// display message to the user
			THROW(("Unable to install handler!\n"));
		}
#else
		signal(SIGABRT, &M6CmdLineDriver::SigHandler);
		signal(SIGTERM, &M6CmdLineDriver::SigHandler);
		signal(SIGINT, &M6CmdLineDriver::SigHandler);
#endif


		if (argc < 2)
		{
			cout << "Usage: m6 command [options]" << endl
				 << endl
				 << "  Command can be one of:" << endl
				 << endl
				 << "    blast       Do a blast search" << endl
				 << "    build       (Re-)build a databank" << endl
				 << "    dump        Dump index data" << endl
				 << "    entry       Retrieve and print an entry" << endl
				 << "    fetch       Fetch/mirror remote data for a databank" << endl
				 << "    info        Display information and statistics for a databank" << endl
				 << "    query       Perform a search in a databank" << endl
				 << "    vacuum      Clean up a databank removing unused data" << endl
				 << "    validate    Perform a set of validation tests" << endl
				 << "    update      Same as build, but does a fetch first" << endl
				 << "    password    Generate password for use in configuration file" << endl
				 << endl
				 << "  Use m6 command --help for more info on each command" << endl;
			exit(1);
		}
		
		M6CmdLineDriver::Exec(argc, argv);
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
