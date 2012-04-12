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
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Error.h"
#include "M6Document.h"
#include "M6Databank.h"
#include "M6DataSource.h"
#include "M6Progress.h"
#include "M6Iterator.h"

using namespace std;
namespace fs = boost::filesystem;
namespace io = boost::iostreams;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

namespace M6Blast {

const uint32
	kM6ResTableCount = 23,
	kM6WordSize = 3,
	kM6MaxWordIndex = kM6ResTableCount * kM6ResTableCount * kM6ResTableCount,
	kM6Bits = 5,
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

const char kM6Residues[] = "ABCDEFGHIKLMNPQRSTVWXYZ";

inline int ResidueNr(char inAA)
{
	const static int8 kResidueNrTable[] = {
	//	A   B   C   D   E   F   G   H   I       K   L   M   N   P   Q   R   S   T  U=X  V   W   X   Y   Z
		0,  1,  2,  3,  4,  5,  6,  7,  8, -1,  9, 10, 11, 12, 13, 14, 15, 16, 17, 20, 18, 19, 20, 21, 22
	};
	
	inAA &= ~(040);
	int result = -1;
	if (inAA >= 'A' and inAA <= 'Z')
		result = kResidueNrTable[inAA - 'A'];
	return result;
}

class M6Matrix
{
  public:

	int				operator()(char inAA1, char inAA2) const;

  private:
	
};

inline int M6Matrix::operator()(char inAA1, char inAA2) const
{
	int rn1 = ResidueNr(inAA1);
	int rn2 = ResidueNr(inAA2);
	
	int result = -4;
	
	if (rn1 >= 0 and rn2 >= 0)
	{
		if (rn1 > rn2)
			result = kM6Blosum62[(rn2 * (rn2 + 1)) / 2 + rn1];
		else
			result = kM6Blosum62[(rn1 * (rn1 + 1)) / 2 + rn2];
	}

	return result;	
}

// --------------------------------------------------------------------

template<int WORDSIZE>
struct M6WordT
{
	uint8&			operator[](uint32 ix)	{ return aa[ix]; }
	const char*		c_str() const			{ return reinterpret_cast<const char*>(aa); }
	size_t			length() const			{ return WORDSIZE; }

	uint8			aa[WORDSIZE + 1];
};

typedef M6WordT<kM6WordSize> M6Word;

class M6PermutationIterator
{
  public:
					M6PermutationIterator(const char* inSequence, const M6Matrix& inMatrix, int32 inThreshold)
						: mMatrix(inMatrix), mThreshold(inThreshold), mIndex(0)
					{
						for (uint32 i = 0; i < kM6WordSize; ++i)
							mWord[i] = inSequence[i];
					}
	
	bool			Next(M6Word& outWord, uint32& outIndex, int32& outScore);

  private:
	M6Word			mWord;
	uint32			mIndex;
	const M6Matrix&	mMatrix;
	int32			mThreshold;
};

bool M6PermutationIterator::Next(M6Word& outWord, uint32& outIndex, int32& outScore)
{
	bool result = false;
	M6Word w;
	w[kM6WordSize] = 0;

	while (mIndex < kM6MaxWordIndex)
	{
		uint32 ix = outIndex = mIndex;
		int32 score = 0;
		
		for (uint32 i = 0; i < kM6WordSize; ++i)
		{
			w[i] = kM6Residues[ix % kM6ResTableCount];
			ix /= kM6ResTableCount;
			score += mMatrix(mWord[i], w[i]);
		}
		
		++mIndex;
		
		if (score >= mThreshold)
		{
			result = true;
			outWord = w;
			outScore = score;
			break;
		}
	}
	
	return result;
}

class M6Accumulator
{
  public:
				M6Accumulator(uint32 inDocCount)
					: mItems(reinterpret_cast<M6Item*>(calloc(sizeof(M6Item), inDocCount)))
					, mFirst(nullptr), mDocCount(inDocCount), mHitCount(0) {}
				
				~M6Accumulator()
				{
					free(mItems);
				}
				
	uint32		Add(uint32 inDocNr, uint32 inDelta)
				{
					if (mItems[inDocNr].mCount++ == 0)
					{
						mItems[inDocNr].mNext = mFirst;
						mFirst = &mItems[inDocNr];
						++mHitCount;
					}
					
					return mItems[inDocNr].mValue += inDelta;
				}

	uint32		operator[](uint32 inIndex) const	{ return mItems[inIndex].mValue; }
	
	void		Collect(vector<uint32>& outDocs)
				{
					outDocs.reserve(mHitCount);
					for (M6Item* item = mFirst; item != nullptr; item = item->mNext)
						outDocs.push_back(static_cast<uint32>(item - mItems));
					
					//if (mHitCount > outDocs.size())
					//	mHitCount = outDocs.size();
				}
	
	uint32		GetHitCount() const					{ return mHitCount; }

  private:
	
	struct M6Item
	{
		uint32	mValue;
		uint32	mCount;
		M6Item*	mNext;
	};
	
	M6Item*		mItems;
	M6Item*		mFirst;
	uint32		mDocCount, mHitCount;
};

void QueryBlastDB(M6Databank& inDatabank, const string& inQuery)
{
	vector<pair<M6Word,int32>> test(kM6MaxWordIndex);
	const M6Matrix M;
	
	for (uint32 i = 0; i + kM6WordSize <= inQuery.length(); ++i)
	{
		M6PermutationIterator iter(inQuery.c_str() + i, M, kM6Threshold);
		M6Word w;
		uint32 index;
		int32 score;
		
		while (iter.Next(w, index, score))
		{
			assert(index < test.size());
			
			test[index].first = w;
			test[index].second += score;
		}
	}
	
	sort(test.begin(), test.end(), [](const pair<M6Word,int32>& a, const pair<M6Word,int32>& b) -> bool {
		return a.second > b.second;
	});
	
	M6Accumulator accu(inDatabank.GetMaxDocNr());
	uint32 Smax = 0;
	
	foreach (auto word, test)
	{
		if (word.second * 100 < Smax)
			break;

		unique_ptr<M6Iterator> iter(inDatabank.Find("seq", word.first.c_str()));

		uint32 docNr; float rank;
		while (iter->Next(docNr, rank))
		{
			uint32 s = accu.Add(docNr, word.second);
			if (Smax < s)
				Smax = s;
		}
	}
	
	vector<uint32> docs;
	accu.Collect(docs);
	
	sort(docs.begin(), docs.end(), [&](uint32 a, uint32 b) -> bool {
		return accu[a] > accu[b];
	});

	for (uint32 i = 0; i < 100; ++i)
	{
		unique_ptr<M6Document> doc(inDatabank.Fetch(docs[i]));
		if (doc)
			cout << doc->GetAttribute("id") << "\t" << doc->GetAttribute("title") << endl
				 << doc->GetText() << endl;
	}
}

// --------------------------------------------------------------------

void AddEntry(M6Databank& inDatabank, M6Lexicon& inLexicon, const string& inEntry)
{
	static boost::regex re("^>(?:[a-z]+\\|)?(?:([A-Za-z0-9_]+)\\|)?([A-Za-z0-9_]+) (.*)\\n((?:[A-IK-Z]+\\n)+)$");
	
	boost::smatch m;
	if (not boost::regex_match(inEntry, m, re, boost::match_not_dot_newline))
		cerr << "no match for: " << inEntry << endl;
	else
	{
		string id = m[2];
		string acc = m[1];
		string title = m[3];
		string seq = m[4];
		
		ba::erase_all(seq, "\n");
		
		unique_ptr<M6InputDocument> doc(new M6InputDocument(inDatabank, inEntry));

		doc->Index("id", eM6StringData, true, id.c_str(), id.length());
		doc->SetAttribute("id", id.c_str(), id.length());

		if (not acc.empty())
		{
			doc->Index("acc", eM6StringData, true, acc.c_str(), acc.length());
			doc->SetAttribute("acc", acc.c_str(), acc.length());
		}

		if (not title.empty())
		{
			doc->Index("title", eM6TextData, true, title.c_str(), title.length());
			doc->SetAttribute("title", acc.c_str(), acc.length());
		}

//		vector<pair<const char*,size_t>> words(seq.length() - kM6WordSize + 1);
//		
//		const char* s = seq.c_str();
//		generate(words.begin(), words.end(), [&]() -> pair<const char*,size_t> {
//			return make_pair(s++, kM6WordSize);
//		});
//		
//		doc->Index("words", words);
		doc->IndexSequence("seq", kM6WordSize, seq.c_str(), seq.length());
		
		doc->Tokenize(inLexicon, 0);
		doc->Compress();

		inDatabank.Store(doc.release());
	}
}

// --------------------------------------------------------------------

void BuildBlastDB()
{
	M6Lexicon lexicon;
	unique_ptr<M6Databank> databank(M6Databank::CreateNew("C:/data/mrs/mini-sprot-fasta.m6"));
	databank->StartBatchImport(lexicon);
	
	fs::path rawFile("C:/data/raw/uniprot/uniprot_sprot.fasta.gz");
//	fs::path rawFile("C:/data/raw/uniprot/mini-sprot.fa");
	
	{
		M6Progress progress(fs::file_size(rawFile), "parsing");
		M6DataSource data(rawFile, progress);

		for (M6DataSource::iterator i = data.begin(); i != data.end(); ++i)
		{
			string entry;
			
			for (;;)
			{
				string line;
				getline(i->mStream, line);
				
				if (line.empty() and i->mStream.eof())
					break;
				
				if (ba::starts_with(line, ">"))
				{
					if (not entry.empty())
						AddEntry(*databank, lexicon, entry);
					entry.clear();
				}
		
				entry += line + "\n";
			}
		
			if (not entry.empty())
				AddEntry(*databank, lexicon, entry);
		}
	}
	
	databank->CommitBatchImport();
}

}

// --------------------------------------------------------------------

int main()
{
	try
	{
//		BuildBlastDB();
		M6Databank databank("C:/data/mrs/mini-sprot-fasta.m6", eReadOnly);
		const char kQuery[] = "MSFCSFFGGEVFQNHFEPGVYVCAKCGYELFSSHSKYAHSSPWPAFTETIHADSVAKRPEHNRPGALKVSCGRCGNGLGHEFLNDGPKRGQSRFUIFSSSLKFIPKGQESSPSQGQ";
		M6Blast::QueryBlastDB(databank, kQuery);
	}
	catch (exception& e)
	{
		cerr << e.what() << endl;
	}
	
	return 0;
}
