#include "M6Lib.h"
#include "M6Index.h"

#include <fstream>
#include <iostream>

#include <boost/filesystem/operations.hpp>
#include <boost/program_options.hpp>

using namespace std;
namespace po = boost::program_options;

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
//			("batch-mode",							"Use batch mode")
//			("verbose,v",							"Be verbose")
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
					result = true;
					break;
				}
				
				if ((nr % 10000) == 0)
				{
					cout << '.';
					cout.flush();
				}

				return result;
			};
		
		if (vm.count("insert-mode"))
		{
			if (fs::exists(filename))
				fs::remove(filename);

			M6SimpleIndex indx(outfile, eReadWrite);
			
			M6Tuple tuple;
			while (data(tuple))
				indx.insert(tuple.key, tuple.value);

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
