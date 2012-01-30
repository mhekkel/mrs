#include "M6Lib.h"

#include <fstream>
#include <iostream>

#include <boost/filesystem/operations.hpp>
#include <boost/program_options.hpp>

#include "M6Error.h"
#include "M6Index.h"

using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

int VERBOSE = 0;

int main(int argc, char* argv[])
{
	try
	{
		po::options_description desc("m6-create options");
		desc.add_options()
			("help,h",								"Display help message")
			("input,i",		po::value<string>(),	"Input file (one entry per line)")
			("output,o",	po::value<string>(),	"Output file (defaults to input-file-name + .ix)")
			("insert-mode",							"Use insert mode")
			("test-mode",	po::value<string>(),	"Test instead of create")
//			("batch-mode",							"Use batch mode")
			("verbose,v",							"Be verbose")
			;

		po::positional_options_description p;
		p.add("input", 1);
		p.add("output", 2);

		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
		po::notify(vm);

		if (vm.count("help") or vm.count("input") == 0)
		{
			cout << desc << "\n";
			exit(1);
		}
		
		if (vm.count("verbose"))
			VERBOSE = 1;
		
		string infile(vm["input"].as<string>()), outfile;
		if (vm.count("output"))
			outfile = vm["output"].as<string>();
		else	
			outfile = infile + ".ix";

		ifstream in(infile);
		uint32 nr = 0;

		M6SortedInputIterator data = 
			[&in, &nr](M6Tuple& outTuple) -> bool
			{
				bool result = false;
				
				while (not in.eof())
				{
					string line;
					getline(in, line);

					if (line.empty())
						break;

					outTuple.key = line;
					outTuple.value = ++nr;
					
					if (VERBOSE and (nr % 1000) == 0)
						cout << line << " (" << nr << ")" << endl;
					
					result = true;
					break;
				}
				
				if ((nr % 100000) == 0)
				{
					cout << '.';
					cout.flush();

					if ((nr % 6000000) == 0)
						cout << ' ' << nr << endl;
				}

				return result;
			};
		
		if (vm.count("test-mode"))
		{
			M6SimpleIndex indx(outfile, eReadOnly);
			
			uint32 n = 0;
			M6Tuple tuple;
			
			if (vm["test-mode"].as<string>() == "iterator")
			{
				M6SimpleIndex::iterator i = indx.begin();
				while (data(tuple) and i != indx.end())
				{
					++n;
					
					if (i->key != tuple.key)
						THROW(("keys don't match: %s <> %s", tuple.key.c_str(), i->key.c_str()));
	
					if (i->value != tuple.value)
						THROW(("Value incorrect (%lld != %lld)", tuple.value, i->value));
					
					++i;
				}
			}
			else
			{
				while (data(tuple))
				{
					++n;
	
					int64 v;
					if (not indx.find(tuple.key, v))
						THROW(("Key '%s' not found", tuple.key.c_str()));
	
					if (v != tuple.value)
						THROW(("Value incorrect (%lld != %lld)", tuple.value, n));
				}
			}
			
			if (n != indx.size())
				THROW(("Invalid index size: %ld != %ld", n, indx.size()));
		}
		else if (vm.count("insert-mode"))
		{
			if (fs::exists(outfile))
				fs::remove(outfile);

			M6SimpleIndex indx(outfile, eReadWrite);
//			indx.SetAutoCommit(false);
			
			M6Tuple tuple;
			while (data(tuple))
				indx.insert(tuple.key, tuple.value);
			
			cout << endl
				 << "Wrote entries, now committing" << endl;
				
			indx.Commit();

			cout << endl
				 << "Created index with:" << endl
				 << "  " << indx.size() << " entries" << endl
				 << "  " << indx.depth() << " depth" << endl;
		}
		else
		{
			M6SimpleIndex indx(outfile, data);
			
			cout << endl
				 << "Created index with:" << endl
				 << "  " << indx.size() << " entries" << endl
				 << "  " << indx.depth() << " depth" << endl;
		}
	}
	catch (exception& e)
	{
		cerr << endl << "Unhandled exception: " << e.what() << endl;
		exit(1);
	}

	return 0;
}
