#include "M6Lib.h"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "M6Error.h"
#include "M6Document.h"
#include "M6Databank.h"

using namespace std;
namespace fs = boost::filesystem;
namespace io = boost::iostreams;

// --------------------------------------------------------------------

const uint32
	kM6ResTableCount = 23,
	kM6Threshold = 11;

const int8 kM6Blosum62[] = {
	  4,                                                                                                               // A
	 -2,   4,                                                                                                          // B
	  0,  -3,   9,                                                                                                     // C
	 -2,   4,  -3,   6,                                                                                                // D
	 -1,   1,  -4,   2,   5,                                                                                           // E
	 -2,  -3,  -2,  -3,  -3,   6,                                                                                      // F
	  0,  -1,  -3,  -1,  -2,  -3,   6,                                                                                 // G
	 -2,   0,  -3,  -1,   0,  -1,  -2,   8,                                                                            // H
	 -1,  -3,  -1,  -3,  -3,   0,  -4,  -3,   4,                                                                       // I
	 -1,   0,  -3,  -1,   1,  -3,  -2,  -1,  -3,   5,                                                                  // K
	 -1,  -4,  -1,  -4,  -3,   0,  -4,  -3,   2,  -2,   4,                                                             // L
	 -1,  -3,  -1,  -3,  -2,   0,  -3,  -2,   1,  -1,   2,   5,                                                        // M
	 -2,   3,  -3,   1,   0,  -3,   0,   1,  -3,   0,  -3,  -2,   6,                                                   // N
	 -1,  -2,  -3,  -1,  -1,  -4,  -2,  -2,  -3,  -1,  -3,  -2,  -2,   7,                                              // P
	 -1,   0,  -3,   0,   2,  -3,  -2,   0,  -3,   1,  -2,   0,   0,  -1,   5,                                         // Q
	 -1,  -1,  -3,  -2,   0,  -3,  -2,   0,  -3,   2,  -2,  -1,   0,  -2,   1,   5,                                    // R
	  1,   0,  -1,   0,   0,  -2,   0,  -1,  -2,   0,  -2,  -1,   1,  -1,   0,  -1,   4,                               // S
	  0,  -1,  -1,  -1,  -1,  -2,  -2,  -2,  -1,  -1,  -1,  -1,   0,  -1,  -1,  -1,   1,   5,                          // T
	  0,  -3,  -1,  -3,  -2,  -1,  -3,  -3,   3,  -2,   1,   1,  -3,  -2,  -2,  -3,  -2,   0,   4,                     // V
	 -3,  -4,  -2,  -4,  -3,   1,  -2,  -2,  -3,  -3,  -2,  -1,  -4,  -4,  -2,  -3,  -3,  -2,  -3,  11,                // W
	  0,  -1,  -2,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -2,  -1,  -1,   0,   0,  -1,  -2,  -1,           // X
	 -2,  -3,  -2,  -3,  -2,   3,  -3,   2,  -1,  -2,  -1,  -1,  -2,  -3,  -1,  -2,  -2,  -2,  -1,   2,  -1,   7,      // Y
	 -1,   1,  -3,   1,   4,  -3,  -2,   0,  -3,   1,  -3,  -1,   0,  -1,   3,   0,   0,  -1,  -2,  -3,  -1,  -2,   4, // Z
};

inline int ResidueNr(char inAA)
{
	const static int kResidueNrTable[] = {
	//	 0   A   B   C   D   E   F   G   H   I       K   L   M   N   P   Q   R   S   T       V   W   X   Y   Z
		-1,  0,  1,  2,  3,  4,  5,  6,  7,  8, -1,  9, 10, 11, 12, 13, 14, 15, 16, 17, -1, 18, 19, 20, 21, 22
	};
	
	inAA &= ~(040);
	int result = -1;
	if (inAA >= 'A' and inAA <= 'Z')
		result = kResidueNrTable[inAA];
	return result;
}

class B6Matrix
{
  public:

	int				operator()(char inAA1, char inAA2) const;

  private:
	
};

inline int B6Matrix::operator()(char inAA1, char inAA2) const
{
	int rn1 = ResidueNr(inAA1);
	int rn2 = ResidueNr(inAA2);
	
	int result = -4;
	
	if (rn1 >= 0 and rn2 >= 0)
	{
		if (rn1 > rn2)
			result = kM6Blosum62[rn1 * kM6ResTableCount
		else
			
		
}

// --------------------------------------------------------------------

void AddEntry(const string& inID, const string& inTitle, const string& inSequence)
{
	
}

// --------------------------------------------------------------------

void BuildBlastDB()
{
	io::filtering_stream<io::input> in;
	in.push(io::gzip_decompressor());
	
	fs::ifstream file("C:/data/raw/uniprot/uniprot_sprot.fasta.gz", ios::binary);
	if (not file.is_open())
		throw M6Exception("openen");
	in.push(file);
	
	for (;;)
	{
		string line;
		getline(in, line);
		
		cout << line << endl;
	}
}

int main()
{
	try
	{
		BuildBlastDB();
	}
	catch (exception& e)
	{
		cerr << e.what() << endl;
	}
	
	return 0;
}
