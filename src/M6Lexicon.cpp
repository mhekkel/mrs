//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//	 (See accompanying file LICENSE_1_0.txt or copy at
//		   http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/algorithm/string.hpp>

#include "M6Error.h"
#include "M6Lexicon.h"
#include "M6Index.h"

using namespace std;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

// M6LexPage stores strings without null terminator packed in s. 
// e is an array with offsets into s, s and e grow towards each other.
// This asserts that space is used optimally.

struct M6LexPage
{
	// pages are 8 MB each:
	static const uint32 kLexDataSize = 8 * 1024 * 1024 - 2 * sizeof(uint32);
	
	uint32		N;
	uint32		first;
	union
	{
		char		s[kLexDataSize];
		uint32		e[kLexDataSize / sizeof(uint32)];
	};
	
				M6LexPage(uint32 inFirst)
					: N(0)
					, first(inFirst)
				{
					e[0] = kLexDataSize;
				}
				
	void		Add(const char* inWord, size_t inWordLength)
				{
					assert(N == 0 or Free() >= inWordLength + sizeof(uint32));
					e[N + 1] = static_cast<uint32>(e[N] - inWordLength);
					memcpy(s + e[N + 1], inWord, inWordLength);
					++N;
				}

	void		GetEntry(uint32 inEntry, const char*& outWord, size_t& outWordLength) const
				{
					assert(inEntry < N);
					assert(e[inEntry + 1] <= e[inEntry]);
					assert(e[inEntry] <= kLexDataSize);
					
					outWord = s + e[inEntry + 1];
					outWordLength = e[inEntry] - e[inEntry + 1];
				}

	int			Compare(const char* inWord, size_t inWordLength, uint32 inEntry) const
				{
					assert(static_cast<int32>(inEntry) < N);
					assert(e[inEntry + 1] < e[inEntry]);
					assert(e[inEntry] <= kLexDataSize);

					size_t l = e[inEntry] - e[inEntry + 1];
					if (l > inWordLength)
						l = inWordLength;

					int d = memcmp(inWord, s + e[inEntry + 1], l);
					if (d == 0)
					{
						if (inWordLength > l)
							d = 1;
						else if (e[inEntry] - e[inEntry + 1] > l)
							d = -1;
					}

					return d;	
				}

	int			Compare(const M6LexPage* inPage, uint32 inPEntry, uint32 inEntry) const
				{
					assert(inPEntry < inPage->N);
					assert(inEntry < N);

					const uint32* e1 = inPage->e;
					const uint32* e2 = e;

					uint32 l1 = e1[inPEntry] - e1[inPEntry + 1];
					uint32 l2 = e2[inEntry] - e2[inEntry + 1];
					
					static const M6BasicComparator comp = M6BasicComparator();
					return comp(inPage->s + e1[inPEntry + 1], l1, s + e2[inEntry + 1], l2);
				}

	bool		TestKeyBit(uint32 inEntry, uint32 inBit) const
				{
					assert(static_cast<int32>(inEntry) < N);
					assert(e[inEntry + 1] < e[inEntry]);
					assert(e[inEntry] <= kLexDataSize);

					bool result = false;
					
					uint32 byte = inBit >> 3;
					if (byte < e[inEntry] - e[inEntry + 1])
					{
						uint32 bit = 7 - (inBit & 0x0007);
						result = (s[e[inEntry + 1] + byte] & (1 << bit)) != 0;
					}
					
					return result;
				}

	uint32		Free() const
				{
					return e[N] - (N + 1) * sizeof(uint32);
				}
};

// --------------------------------------------------------------------

struct M6LexiconImpl
{
	// M6Node is a node in a Patricia tree
	// and we store kIxNodeCount node in a page
	
	static const uint32 kIxNodeCount = 8 * 1024;
	
	struct M6Node
	{
		M6Node*			left;
		M6Node*			right;
		uint32			value;
		uint32			bit;
	};
	
					M6LexiconImpl();
					~M6LexiconImpl();

	uint32			Lookup(const char* inWord, size_t inWordLength) const;
	uint32			Store(const char* inWord, size_t inWordLength);
	void			GetString(uint32 inNr, const char*& outWord, size_t& outWordLength) const;
	const M6Node*	Find(const M6Node* inNode, const char* inWord, size_t inWordLength) const;
	bool			Find(const char* inWord, size_t inWordLength, uint32& outNr) const;
	int				Compare(const char* inWord, size_t inWordLength, uint32 inNr) const;
	int				Compare(uint32 inA, uint32 inB) const;

	bool			TestKeyBit(const char* inWord, size_t inWordLength, uint32 inBit) const
					{
						bool result = false;
						
						uint32 byte = inBit >> 3;
						if (byte < inWordLength)
						{
							uint32 bit = 7 - (inBit & 0x0007);
							result = (inWord[byte] & (1 << bit)) != 0;
						}
						
						return result;
					}

	bool			CompareKeyBits(const char* inWordA, size_t inWordALength,
						const char* inWordB, size_t inWordBLength, uint32 inBit) const
					{
						return
							TestKeyBit(inWordA, inWordALength, inBit) ==
							TestKeyBit(inWordB, inWordBLength, inBit);
					}

	bool			CompareKeyBits(const char* inWord, size_t inWordLength, uint32 inNr, uint32 inBit) const;


	typedef vector<M6LexPage*> LexPageArray;

	LexPageArray::const_iterator
					GetPage(uint32& ioNr) const;

	typedef vector<M6Node*> NodePageArray;

	M6Node*			AllocateNode();

	LexPageArray	mPages;
	NodePageArray	mNodes;
	M6Node*			mRoot;
	uint32			mCount;
};

// --------------------------------------------------------------------

M6LexiconImpl::M6LexiconImpl()
	: mCount(0)
{
	mPages.push_back(new M6LexPage(0));

	mRoot = AllocateNode();
	
	mRoot->value = 0;
	mRoot->bit = 0;
	mRoot->left = mRoot;
	mRoot->right = mRoot;

	mPages.front()->Add(" ", 1);
	mCount = 1;
}

M6LexiconImpl::~M6LexiconImpl()
{
	for (LexPageArray::iterator p = mPages.begin(); p != mPages.end(); ++p)
		delete *p;

	for (NodePageArray::iterator p = mNodes.begin(); p != mNodes.end(); ++p)
		delete[] *p;
}

M6LexiconImpl::M6Node* M6LexiconImpl::AllocateNode()
{
	uint32 ix = mCount % kIxNodeCount;
	
	if (ix == 0)	// time for a new node page
	{
		assert((mCount / kIxNodeCount) == mNodes.size());
		mNodes.push_back(new M6Node[kIxNodeCount]);
	}
	
	return mNodes.back() + ix;
}

inline M6LexiconImpl::LexPageArray::const_iterator
M6LexiconImpl::GetPage(uint32& ioNr) const
{
	int32 L = 0, R = static_cast<int32>(mPages.size() - 1);
	while (L <= R)
	{
		int32 i = (L + R) / 2;
		
		if (mPages[i]->first <= ioNr)
			L = i + 1;
		else
			R = i - 1;
	}
	
	LexPageArray::const_iterator p = mPages.begin() + L - 1;

	assert(L > 0);
	assert(uint32(L - 1) < mPages.size());
	assert((*p)->first <= ioNr);
	assert(ioNr < (*p)->first + (*p)->N);
	
	ioNr -= (*p)->first;
	
	return p;
}

const M6LexiconImpl::M6Node* M6LexiconImpl::Find(const M6Node* inNode, const char* inWord, size_t inWordLength) const
{
	const M6Node* p;
	
	do
	{
		p = inNode;
		
		if (TestKeyBit(inWord, inWordLength, inNode->bit))
			inNode = inNode->right;
		else
			inNode = inNode->left;
	}
	while (p->bit < inNode->bit);
	
	return inNode;
}

bool M6LexiconImpl::Find(const char* inWord, size_t inWordLength, uint32& outNr) const
{
	bool result = false;
	
	if (mCount > 0)
	{
		const M6Node* t = Find(mRoot, inWord, inWordLength);
		if (t != nullptr and Compare(inWord, inWordLength, t->value) == 0)
		{
			result = true;
			outNr = t->value;
		}
	}
	
	return result;
}

uint32 M6LexiconImpl::Lookup(const char* inWord, size_t inWordLength) const
{
	uint32 result = 0;

	const M6Node* t = Find(mRoot, inWord, inWordLength);

	if (t != mRoot and Compare(inWord, inWordLength, t->value) == 0)
		result = t->value;
	
	return result;
}

uint32 M6LexiconImpl::Store(const char* inWord, size_t inWordLength)
{
	// check first to see if something has been added to inNode
	// while we were locked...

	const M6Node* t = Find(mRoot, inWord, inWordLength);

	if (t != mRoot and Compare(inWord, inWordLength, t->value) == 0)
		return t->value;

	uint32 i = 0;
	
	while (CompareKeyBits(inWord, inWordLength, t->value, i))
		++i;

	M6Node* p;
	M6Node* x = mRoot;	
	
	do
	{
		p = x;
		if (TestKeyBit(inWord, inWordLength, x->bit))
			x = x->right;
		else
			x = x->left;
	}
	while (x->bit < i and p->bit < x->bit);
	
	if (mPages.back()->Free() < inWordLength + sizeof(uint32))
		mPages.push_back(new M6LexPage(mCount));

	mPages.back()->Add(inWord, inWordLength);

	M6Node* n = AllocateNode();
	n->value = mCount;
	n->bit = i;
	
	if (TestKeyBit(inWord, inWordLength, n->bit))
	{
		n->right = n;
		n->left = x;
	}
	else
	{
		n->right = x;
		n->left = n;
	}
	
	if (TestKeyBit(inWord, inWordLength, p->bit))
		p->right = n;
	else
		p->left = n;
	
	uint32 result = mCount;
	
	++mCount;

	return result;
}

inline void M6LexiconImpl::GetString(uint32 inNr, const char*& outWord, size_t& outWordLength) const
{
	LexPageArray::const_iterator p = GetPage(inNr);
	
	if (p == mPages.end() or (*p)->N == 0 or inNr >= (*p)->N)
		THROW(("Lexicon is invalid"));
	
	return (*p)->GetEntry(inNr, outWord, outWordLength);
}

int M6LexiconImpl::Compare(const char* inWord, size_t inWordLength, uint32 inNr) const
{
	LexPageArray::const_iterator p = GetPage(inNr);

	if (p == mPages.end() or (*p)->N == 0 or inNr >= (*p)->N)
		THROW(("Lexicon is invalid"));
	
	return (*p)->Compare(inWord, inWordLength, inNr);
}

int M6LexiconImpl::Compare(uint32 inA, uint32 inB) const
{
	int result = 0;
	
	if (inA != inB)
	{
		LexPageArray::const_iterator a = GetPage(inA);
		LexPageArray::const_iterator b = GetPage(inB);
		
		assert(a != mPages.end());
		assert(b != mPages.end());
	
		if (a != mPages.end() and inA < (*a)->N and
			b != mPages.end() and inB < (*b)->N)
		{
			result = (*b)->Compare(*a, inA, inB);
		}
		else
			assert(false);
	}
	
	return result;
}

bool M6LexiconImpl::CompareKeyBits(const char* inWord, size_t inWordLength, uint32 inNr, uint32 inBit) const
{
	LexPageArray::const_iterator p = GetPage(inNr);

	if (p == mPages.end() or (*p)->N == 0 or inNr >= (*p)->N)
		THROW(("Lexicon is invalid"));
	
	return
		TestKeyBit(inWord, inWordLength, inBit) == (*p)->TestKeyBit(inNr, inBit);
}

// --------------------------------------------------------------------

M6Lexicon::M6Lexicon()
	: mImpl(new M6LexiconImpl)
{
}

M6Lexicon::~M6Lexicon()
{
	delete mImpl;
}

uint32 M6Lexicon::Lookup(const char* inWord, size_t inWordLength) const
{
	return mImpl->Lookup(inWord, inWordLength);
}

uint32 M6Lexicon::Store(const char* inWord, size_t inWordLength)
{
	return mImpl->Store(inWord, inWordLength);
}

void M6Lexicon::GetString(uint32 inNr, const char*& outWord, size_t& outWordLength) const
{
	return mImpl->GetString(inNr, outWord, outWordLength);
}

int M6Lexicon::Compare(uint32 inA, uint32 inB) const
{
	return mImpl->Compare(inA, inB);
}

uint32 M6Lexicon::Count() const
{
	return mImpl->mCount;
}
