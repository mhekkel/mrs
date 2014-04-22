#include "M6Lib.h"

#include <iostream>
#include <set>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include <zeep/xml/document.hpp>

#include "M6Config.h"
#include "M6Error.h"
#include "M6BlastCache.h"

using namespace std;

namespace io = boost::iostreams;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

// Checks the blast cache for corrupt files:

int main(int argc, char* argv[])
{
	if(argc!=2)
	{
		cout << "Please give a cache directory path!\n" ;
		return 0;
	}

	fs::path cacheDir(argv[1]);
	fs::directory_iterator end_iter;
	for( fs::directory_iterator dir_iter(cacheDir) ; dir_iter != end_iter ; ++dir_iter)
	{
		fs::path path=dir_iter->path();
		if(!boost::filesystem::is_regular_file(path))
			continue;

		string filename=path.filename().string();

		io::filtering_stream<io::input> in;
		fs::ifstream file(path, ios::binary);
		if (not file.is_open()) {
			cerr << filename << ": file not open\n";
			return 1;
		}

		if ( ba::ends_with( filename, ".bz2" ) )
			in.push(io::bzip2_decompressor());

		in.push(file);

		stringstream stack;
		try
		{
			if ( ba::ends_with( filename, ".xml.bz2" ) )
			{
				stack << "parsing xml\n";

				zeep::xml::document doc(in);

				stack << "converting to blast result\n";

				M6BlastResultPtr result(new M6Blast::Result);

				doc.deserialize("blast-result", const_cast<M6Blast::Result&>(*result));
			}
			else if ( ba::ends_with( filename, ".job" ) )
			{
				stack << "parsing xml\n";

				zeep::xml::document doc(in);

				stack << "converting to blast job\n";

				M6BlastJob job;
		
				doc.deserialize("blastjob", job);
			}
			else if ( ba::ends_with( filename, ".err" ) )
			{
				stack << "reading file\n";

				string line;
				stringstream ss;
				while(!in.eof())
				{
					getline(in, line);
					ss << line << endl;
				}
			}

			stack << "done with this file\n";
		}
		catch(exception& e)
		{
			cout << stack.str();

			cout << path.string() <<" - error: "<< e.what() << endl ; 
		}
	}
	return 0;
}
