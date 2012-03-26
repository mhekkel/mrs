#include "M6Lib.h"

#include <set>

#include <boost/static_assert.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Dictionary.h"
#include "M6Error.h"
#include "M6File.h"
#include "M6Progress.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

// The M6Transition type was altered in version 5.1 of MRS
// to be able to construct larger dictionaries.
// Therefore we need a way to detect the version of dictionary files.
// To do so, we write a new header signature in the new version
// and we append a NULL int32 at the end to avoid older software
// to use the new files.

uint32
	kM6MinWordLength = 4,
	kM6MinWordOccurrence = 4;

struct suggestion
{
	string	word;
	float	idf;
	
	bool	operator<(const suggestion& rhs) const	{ return idf < rhs.idf; }
};

struct M6Suggestions : public vector<suggestion> {};

union M6Transition
{
	struct
	{
		uint8		attr	: 8;
		bool		last	: 1;
		bool		term	: 1;
		uint16		df;
		uint32		dest;
	}				b;
	uint64			d;
};

static_assert(sizeof(M6Transition) == sizeof(int64), "Transition type is of incorrect size");

const int32
	kMatchReward		= 1,
	kDeletePenalty		= -1,
	kInsertPenalty		= -4,
	kSubstitutePenalty	= -2,
	kTransposePenalty	= -2;

const uint32
	kMaxStringLength		= 256,
	kMaxChars				= 256,
	kHashTableSize			= (1 << 20),
	kHashTableElementSize	= (1 << 10),
//	kMaxAutomatonSize		= (1 << 22);
	kMaxAutomatonSize		= numeric_limits<uint32>::max();

class M6Automaton
{
  public:
			M6Automaton(M6File& inFile);
			~M6Automaton();

	void	SuggestCorrection(const string& inWord, vector<string>& outCorrections) const;
	void	SuggestSearchTerms(const string& inWord, M6Suggestions& outSearchTerms) const;

  private:

	uint32	mAutomatonLength;
	uint32	mDocCount;
	M6Transition*
			mAutomaton;
};

class M6HashTable
{
  public:
	typedef vector<M6Transition> M6AutomatonType;

					M6HashTable();
					~M6HashTable();
	
	uint32			Hash(M6Transition* state, uint32 state_len);
	uint32			Lookup(M6Transition* state, uint32 state_len, M6AutomatonType& automaton);
					
  private:

	struct bucket
	{
		uint32			addr;
		uint32			size;
		uint32			next;
	};

	uint32*			table;
	vector<bucket>	buckets;
	int32			last_pos;
};

M6HashTable::M6HashTable()
	: table(new uint32[kHashTableSize]), last_pos(-1)
{
	memset(table, 0, kHashTableSize * sizeof(uint32));
	buckets.push_back(bucket());	// dummy
}

M6HashTable::~M6HashTable()
{
	delete[] table;
}

uint32 M6HashTable::Hash(M6Transition* state, uint32 state_len)
{
	uint64 r = 0;

	for (uint32 i = 0; i < state_len; ++i)
		r += state[i].d;
	
	return ((r * 324027) >> 13) % kHashTableSize;
}

uint32 M6HashTable::Lookup(M6Transition* state, uint32 state_len, M6AutomatonType& automaton)
{
	if (state_len == 0)
		state[state_len++].d = 0;

	state[state_len - 1].b.last = true;

	uint32 addr = Hash(state, state_len);
	bool found = false;
	
	for (uint32 ix = table[addr]; ix != 0 and not found; ix = buckets[ix].next)
	{
		if (buckets[ix].size == state_len)
		{
			found = true;
			
			for (uint32 i = 0; i < state_len; ++i)
			{
				if (automaton[buckets[ix].addr + i].d != state[i].d)
				{
					found = false;
					break;
				}
			}
			
			if (found)
				addr = buckets[ix].addr;
		}
	}
	
	if (not found)
	{
		uint32 size = static_cast<uint32>(automaton.size());
		
		if (size >= kMaxAutomatonSize)
			THROW(("Dictionary overflow"));
		
		for (uint32 i = 0; i < state_len; ++i)
			automaton.push_back(state[i]);
		
		bucket b;
		b.addr = size;
		b.size = state_len;
		b.next = table[addr];
		table[addr] = static_cast<uint32>(buckets.size());
		
		addr = size;
		
		buckets.push_back(b);
	}
	
	return addr;
}

const uint32
	kMaxScoreTableSize = 20,
	kMaxEdits = 2;

struct M6ScoreTableImp
{
	struct M6Score
	{
		string		term;
		int16		score;
		uint16		df;
		
					M6Score()
						: score(0), df(0) {}
					M6Score(const M6Score& rhs)
						: term(rhs.term), score(rhs.score), df(rhs.df) {}
					M6Score(string inTerm, int16 inScore, uint16 inDF)
						: term(inTerm), score(inScore), df(inDF) {}
		
		bool		operator<(const M6Score& rhs) const
						{ return score < rhs.score; }
		bool		operator>(const M6Score& rhs) const
						{ return score > rhs.score; }
	};

					M6ScoreTableImp(const M6Transition* inAutomaton,
						uint32 inAutomatonLength, const string& inWord);

	void			Test(uint32 inState, int16 inScore, uint32 inEdits,
						string inMatch, const char* inWoord);
	bool			Match(uint32 inState, int16 inScore, uint32 inEdits,
						string inMatch, const char* inWoord);
	void			Delete(uint32 inState, int16 inScore, uint32 inEdits,
						string inMatch, const char* inWoord);
	void			Insert(uint32 inState, int16 inScore, uint32 inEdits,
						string inMatch, const char* inWoord);
	void			Transpose(uint32 inState, int16 inScore, uint32 inEdits,
						string inMatch, const char* inWoord);
	void			Substitute(uint32 inState, int16 inScore, uint32 inEdits,
						string inMatch, const char* inWoord);

	void			Add(string inTerm, int16 inScore, uint16 inDF);
	void			Finish();
	int32			MinScore();

	const M6Transition*
					mAutomaton;
	uint32			mAutomatonLength;
	M6Score			scores[kMaxScoreTableSize];
	uint32			n;
};

M6ScoreTableImp::M6ScoreTableImp(const M6Transition* inAutomaton,
	uint32 inAutomatonLength, const string& inWord)
	: mAutomaton(inAutomaton), mAutomatonLength(inAutomatonLength), n(0)
{
	string match;
	Test(mAutomaton[mAutomatonLength - 1].b.dest, 0, 0, match, inWord.c_str());

	Finish();
}

void M6ScoreTableImp::Test(uint32 inState, int16 inScore, uint32 inEdits, string inMatch, const char* inWord)
{
	Match(inState, inScore, inEdits, inMatch, inWord);

	if (inScore >= max(MinScore() - 3, 0) and inEdits < 3)
	{
		Delete(inState, inScore, inEdits, inMatch, inWord);
		Insert(inState, inScore, inEdits, inMatch, inWord);
		Transpose(inState, inScore, inEdits, inMatch, inWord);
		Substitute(inState, inScore, inEdits, inMatch, inWord);
	}
}

bool M6ScoreTableImp::Match(uint32 inState, int16 inScore, uint32 inEdits, string inMatch, const char* inWord)
{
	bool match = false;
	
	if (*inWord != 0)
	{
		match = true;
		
		while (mAutomaton[inState].b.attr != inWord[0])
		{
			if (mAutomaton[inState].b.last)
			{
				match = false;
				break;
			}
			
			++inState;
		}
		
		if (match)
		{
			inScore += kMatchReward;
			inMatch += inWord[0];
			
			if (mAutomaton[inState].b.term and inEdits < kMaxEdits)
				Add(inMatch, inScore, mAutomaton[inState].b.df);
			
			Test(mAutomaton[inState].b.dest, inScore, inEdits, inMatch, inWord + 1);
		}
	}
	
	return match;
}

void M6ScoreTableImp::Delete(uint32 inState, int16 inScore, uint32 inEdits, string inMatch, const char* inWord)
{
	uint32 state = inState;

	for (;;)
	{
		char ch = mAutomaton[state].b.attr;

		if (mAutomaton[state].b.term and inEdits < kMaxEdits)
			Add(inMatch + ch, inScore + kDeletePenalty, mAutomaton[state].b.df);

		Test(mAutomaton[state].b.dest, inScore + kDeletePenalty, inEdits + 1, inMatch + ch, inWord);

		if (mAutomaton[state].b.last)
			break;

		++state;
	}
}

void M6ScoreTableImp::Insert(uint32 inState, int16 inScore, uint32 inEdits, string inMatch, const char* inWord)
{
	if (*inWord != 0)
		Test(inState, inScore + kInsertPenalty, inEdits + 1, inMatch, inWord + 1);
}

void M6ScoreTableImp::Transpose(uint32 inState, int16 inScore, uint32 inEdits, string inMatch, const char* inWord)
{
	if (inWord[0] != 0 and inWord[1] != 0)
	{
		string woord;
		woord = inWord[1];
		woord += inWord[0];
		woord += inWord + 2;
		Test(inState, inScore + kTransposePenalty, inEdits + 1, inMatch, woord.c_str());
	}
}

void M6ScoreTableImp::Substitute(uint32 inState, int16 inScore, uint32 inEdits, string inMatch, const char* inWord)
{
	if (inWord[0] != 0)
	{
		uint32 state = inState;
	
		for (;;)
		{
			char ch = mAutomaton[state].b.attr;

			if (mAutomaton[state].b.term and inEdits < kMaxEdits)
				Add(inMatch + ch, inScore + kSubstitutePenalty, mAutomaton[state].b.df);

			Test(mAutomaton[state].b.dest, inScore + kSubstitutePenalty, inEdits + 1, inMatch + ch, inWord + 1);
			
			if (mAutomaton[state].b.last)
				break;

			++state;
		}
	}
}

void M6ScoreTableImp::Add(string inTerm, int16 inScore, uint16 inDF)
{
	if (n >= kMaxScoreTableSize)
	{
		if (inScore > scores[0].score)
		{
			pop_heap(scores, scores + n, greater<M6Score>());
			scores[kMaxScoreTableSize - 1] = M6Score(inTerm, inScore, inDF);
			push_heap(scores, scores + n, greater<M6Score>());
		}
	}
	else
	{
		scores[n] = M6Score(inTerm, inScore, inDF);
		++n;
		push_heap(scores, scores + n, greater<M6Score>());
	}
}

void M6ScoreTableImp::Finish()
{
	sort_heap(scores, scores + n, greater<M6Score>());
}

int32 M6ScoreTableImp::MinScore()
{
	int32 result = 0;
	if (n > 0)
		result = scores[0].score;
	return result;
}

void ExtendSuggestion(uint32 inDocCount,
	const M6Transition*	inAutomaton, uint32 inAutomatonLength,
	uint32 inState, const string& inWord, M6Suggestions& outWords)
{
	uint32 state = inState;

	for (;;)
	{
		char ch = inAutomaton[state].b.attr;

		if (inAutomaton[state].b.term)
		{
			suggestion s;
			s.word = inWord + ch;
			s.idf = static_cast<float>(log(1.0 + inDocCount / inAutomaton[state].b.df));
			
			outWords.push_back(s);
			push_heap(outWords.begin(), outWords.end());
			
			if (outWords.size() > 100)
			{
				pop_heap(outWords.begin(), outWords.end());
				outWords.erase(outWords.end() - 1);
			}
		}

		if (inAutomaton[state].b.dest != 0)
			ExtendSuggestion(inDocCount, inAutomaton, inAutomatonLength, inAutomaton[state].b.dest, inWord + ch, outWords);

		if (inAutomaton[state].b.last)
			break;

		++state;
	}
}

M6Automaton::M6Automaton(M6File& inFile)
{
	inFile.Read(&mDocCount, sizeof(mDocCount));
	inFile.Read(&mAutomatonLength, sizeof(mAutomatonLength));
	
	mAutomaton = new M6Transition[mAutomatonLength];
	inFile.Read(mAutomaton, mAutomatonLength * sizeof(M6Transition)); 
}

M6Automaton::~M6Automaton()
{
	delete[] mAutomaton;
}

void M6Automaton::SuggestCorrection(const string& inWord, vector<string>& outCorrections) const
{
	M6ScoreTableImp scores(mAutomaton, mAutomatonLength, inWord);

	uint16 minDF = 0;
	if (scores.n > 0)
		minDF = scores.scores[0].df;

	//for (uint32 i = 0; i < scores.n; ++i)
	//{
	//	if (minDF < scores.scores[i].df)
	//		minDF = scores.scores[i].df;
	//}
	
	set<string> unique;
	for (uint32 i = 0; i < scores.n; ++i)
	{
		const string& term = scores.scores[i].term;

		if (unique.count(term) or term == inWord)
			continue;

		if (scores.scores[i].df >= minDF)
		{
			outCorrections.push_back(term);
			unique.insert(term);
		}
	}
}

void M6Automaton::SuggestSearchTerms(const string& inWord, M6Suggestions& outSearchTerms) const
{
	string word = ba::to_lower_copy(inWord);
	
	uint32 state = mAutomaton[mAutomatonLength - 1].b.dest;
	
	bool match = true;
	
	for (string::iterator ch = word.begin(); match and ch != word.end(); ++ch)
	{
		match = true;
		
		while (mAutomaton[state].b.attr != *ch)
		{
			if (mAutomaton[state].b.last)
			{
				match = false;
				break;
			}
			
			++state;
		}

		state = mAutomaton[state].b.dest;
	}
	
	if (match)
		ExtendSuggestion(mDocCount, mAutomaton, mAutomatonLength, state, inWord, outSearchTerms);
}

// --------------------------------------------------------------------
//
//	M6Dictionary
//

M6Dictionary::M6Dictionary(fs::path inFile)
	: mAutomaton(nullptr)
{
	if (not fs::exists(inFile))
		THROW(("Dictionary %s does not exist", inFile.string().c_str()));
	
	M6File file(inFile, eReadOnly);
	mAutomaton = new M6Automaton(file);
}

M6Dictionary::~M6Dictionary()
{
	delete mAutomaton;
}

void M6Dictionary::Create(M6BasicIndex& inIndex, uint32 inDocCount,
	M6File& inFile, M6Progress& inProgress)
{
	vector<M6Transition> automaton;
	
	unsigned char s0[kMaxStringLength] = "";
	
	M6Transition larval_state[kMaxStringLength + 1][kMaxChars];
	uint32 l_state_len[kMaxStringLength + 1] = {};
	bool is_terminal[kMaxStringLength];
	uint16 df[kMaxStringLength];	// document frequency
	
	M6HashTable ht;
	uint32 i = 0, p, q, nr = 0;

	for (M6BasicIndex::iterator iter = inIndex.begin(); iter != inIndex.end(); ++iter)
	{
		if (++nr % 10000 == 0)
			inProgress.Progress(nr);

		uint32 dfI = iter.GetCount();
		if (dfI < kM6MinWordOccurrence)
			continue;
		
		string s = *iter;
		q = static_cast<uint32>(s.length());
		if (q < kM6MinWordLength)
			continue;
		
		// calculate the document frequency for this term
		if (dfI > numeric_limits<uint16>::max())
			dfI = numeric_limits<uint16>::max();
		
		for (p = 0; s[p] == s0[p]; ++p)
			;
		
		if (uint8(s[p]) < uint8(s0[p]))
			THROW(("error, strings are unsorted: '%s' >= '%s'", s.c_str(), s0));
		
		while (i > p)
		{
			M6Transition new_trans = {};

			new_trans.b.dest = ht.Lookup(larval_state[i], l_state_len[i], automaton);
			new_trans.b.term = is_terminal[i];
			new_trans.b.df = df[i];
			new_trans.b.attr = s0[--i];
			
			larval_state[i][l_state_len[i]++] = new_trans;
		}
		
		while (i < q)
		{
			s0[i] = s[i];
			is_terminal[++i] = 0;
			df[i] = 0;
			l_state_len[i] = 0;
		}
		
		s0[q] = 0;
		is_terminal[q] = 1;
		df[q] = static_cast<uint16>(dfI);
	}
	
	while (i > 0)
	{
		M6Transition new_trans = {};

		new_trans.b.dest = ht.Lookup(larval_state[i], l_state_len[i], automaton);
		new_trans.b.term = is_terminal[i];
		new_trans.b.df = df[i];
		new_trans.b.attr = s0[--i];
		
		larval_state[i][l_state_len[i]++] = new_trans;
	}
	
	uint32 start_state = ht.Lookup(larval_state[0], l_state_len[0], automaton);
	
	M6Transition t = {};
	t.b.dest = start_state;
	automaton.push_back(t);
	
	inProgress.Progress(inIndex.size());

	inFile.Write(&inDocCount, sizeof(uint32));
	uint32 size = automaton.size();
	inFile.Write(&size, sizeof(uint32));
	inFile.Write(&automaton[0], automaton.size() * sizeof(M6Transition));
}

void M6Dictionary::SuggestCorrection(const string& inWord, vector<string>& outCorrections)
{
	mAutomaton->SuggestCorrection(inWord, outCorrections);
}

void M6Dictionary::SuggestSearchTerms(const string& inWord, vector<string>& outSearchTerms)
{
	M6Suggestions suggestions;
	mAutomaton->SuggestSearchTerms(inWord, suggestions);
	
	sort_heap(suggestions.begin(), suggestions.end());
	
	set<string> words;
	foreach (suggestion& s, suggestions)
	{
		if (words.count(s.word))
			continue;
		words.insert(s.word);
		
		outSearchTerms.push_back(s.word);
	}
}
