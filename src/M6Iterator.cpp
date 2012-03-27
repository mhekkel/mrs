#include "M6Lib.h"

#include <cassert>

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Iterator.h"

using namespace std;
namespace fs = boost::filesystem;

void M6Iterator::Intersect(vector<uint32>& ioDocs, M6Iterator* inIterator)
{
	// merge boolean filter result and ranked results
	vector<uint32> docs;
	swap(docs, ioDocs);
	ioDocs.reserve(docs.size());
	
	uint32 db;
	float r;
	bool empty = not inIterator->Next(db, r);
	vector<uint32>::iterator dr = docs.begin();
	
	while (not empty and dr != docs.end())
	{
		if (*dr == db)
		{
			ioDocs.push_back(db);
			++dr;
			empty = not inIterator->Next(db, r);
		}
		else if (*dr < db)
			++dr;
		else
			inIterator->Next(db, r);
	}
}

// --------------------------------------------------------------------

M6NotIterator::M6NotIterator(M6Iterator* inIter, uint32 inMax)
	: mIter(inIter)
	, mCur(0)
	, mNext(0)
	, mMax(inMax)
{
	mCount = inMax;
	if (inIter != nullptr)
		mCount -= inIter->GetCount();
}

bool M6NotIterator::Next(uint32& outDoc, float& outRank)
{
	for (;;)
	{
		++mCur;
		outDoc = mCur;
		if (mCur < mNext or mNext == 0 or mIter == nullptr)
			break;
		
		assert(mCur == mNext);
		float rank;
		if (not mIter->Next(mNext, rank))
			mNext = 0;
	}

	return outDoc <= mMax;
}

// --------------------------------------------------------------------

M6UnionIterator::M6UnionIterator()
{
}

M6UnionIterator::M6UnionIterator(M6Iterator* inA, M6Iterator* inB)
{
	AddIterator(inA);
	AddIterator(inB);
}

M6UnionIterator::~M6UnionIterator()
{
	foreach (M6IteratorPart& part, mIterators)
		delete part.mIter;
	mIterators.clear();
}

void M6UnionIterator::AddIterator(M6Iterator* inIter)
{
	if (inIter != nullptr)
	{
		M6IteratorPart p = { inIter };
	
		float r;
		if (inIter->Next(p.mDoc, r))
		{
			mIterators.push_back(p);
			push_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
		}
		else
			delete inIter;
	}
}

bool M6UnionIterator::Next(uint32& outDoc, float& outRank)
{
	bool result = false;

	if (not mIterators.empty())
	{
		pop_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
		
		outDoc = mIterators.back().mDoc;
		outRank = 1.0f;
		result = true;

		float r;
		if (mIterators.back().mIter->Next(mIterators.back().mDoc, r))
			push_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
		else
		{
			delete mIterators.back().mIter;
			mIterators.pop_back();
		}
		
		while (not mIterators.empty() and mIterators.front().mDoc <= outDoc)
		{
			pop_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
			
			if (mIterators.back().mIter->Next(mIterators.back().mDoc, r))
				push_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
			else
			{
				delete mIterators.back().mIter;
				mIterators.pop_back();
			}
		}
	}
	
	return result;
}

M6Iterator* M6UnionIterator::Create(M6Iterator* inA, M6Iterator* inB)
{
	M6Iterator* result;
	if (inA == nullptr)
		result = inB;
	else if (inB == nullptr)
		result = inA;
	else if (dynamic_cast<M6UnionIterator*>(inA) != nullptr)
		static_cast<M6UnionIterator*>(inA)->AddIterator(inB);
	else if (dynamic_cast<M6UnionIterator*>(inB) != nullptr)
		static_cast<M6UnionIterator*>(inB)->AddIterator(inA);
	else
		result = new M6UnionIterator(inA, inB);
	return result;
}

// --------------------------------------------------------------------

M6IntersectionIterator::M6IntersectionIterator()
{
}

M6IntersectionIterator::M6IntersectionIterator(M6Iterator* inA, M6Iterator* inB)
{
	if (inA != nullptr and inB != nullptr)
	{
		AddIterator(inA);
		AddIterator(inB);
	}
	else
	{
		delete inA;
		delete inB;
	}
}

M6IntersectionIterator::~M6IntersectionIterator()
{
	foreach (M6IteratorPart& part, mIterators)
		delete part.mIter;
	mIterators.clear();
}

void M6IntersectionIterator::AddIterator(M6Iterator* inIter)
{
	if (inIter == nullptr)
	{
		foreach (M6IteratorPart& part, mIterators)
			delete part.mIter;
		mIterators.clear();
	}
	else
	{
		M6IteratorPart p = { inIter };
	
		float r;
		if (inIter->Next(p.mDoc, r))
			mIterators.push_back(p);
		else
			delete inIter;
	}
}

bool M6IntersectionIterator::Next(uint32& outDoc, float& outRank)
{
	bool result = false, done = mIterators.empty();
	float r;
	
	while (not (result or done))
	{
		sort(mIterators.begin(), mIterators.end());

		outDoc = mIterators.back().mDoc;

		foreach (M6IteratorPart& part, mIterators)
		{
			while (part.mDoc < outDoc)
			{
				if (not part.mIter->Next(part.mDoc, r))
				{
					done = true;
					break;
				}
			}
		}

		if (mIterators.front().mDoc == outDoc)
		{
			result = true;
			foreach (M6IteratorPart& part, mIterators)
				done = done or part.mIter->Next(part.mDoc, r) == false;
			break;
		}

		if (not mIterators.back().mIter->Next(mIterators.back().mDoc, r))
			done = true;
	}

	if (done)
	{
		foreach (M6IteratorPart& part, mIterators)
			delete part.mIter;
		mIterators.clear();
	}

	return result;
}

M6Iterator* M6IntersectionIterator::Create(M6Iterator* inA, M6Iterator* inB)
{
	M6Iterator* result = nullptr;

	if (inA != nullptr and inB != nullptr)
		result = new M6IntersectionIterator(inA, inB);
	else
	{
		delete inA;
		delete inB;
	}

	return result;
}

// --------------------------------------------------------------------

void M6PhraseIterator::M6PhraseIteratorPart::ReadArray()
{
	::ReadArray(mBits, mIDL);
	transform(mIDL.begin(), mIDL.end(), mIDL.begin(), [=](uint32 ioOffset) -> uint32 { return ioOffset - mIndex; });
}

M6PhraseIterator::M6PhraseIterator(fs::path& inIDLFile,
	vector<std::tr1::tuple<M6Iterator*,int64,uint32>>& inIterators)
	: mIDLFile(inIDLFile, eReadOnly)
{
	bool ok = true;
	
	foreach (auto i, inIterators)
	{
		M6PhraseIteratorPart p = {
			tr1::get<0>(i), M6IBitStream(mIDLFile, tr1::get<1>(i)), tr1::get<2>(i) };
	
		float r;
		if (not p.mIter->Next(p.mDoc, r))
		{
			ok = false;
			break;
		}
		
		mIterators.push_back(p);
		mIterators.back().ReadArray();
	}
	
	if (not ok)
	{
		foreach (auto& iter, inIterators)
			delete tr1::get<0>(iter);
		mIterators.clear();
	}
}

M6PhraseIterator::~M6PhraseIterator()
{
	foreach (auto& part, mIterators)
		delete part.mIter;
	mIterators.clear();
}

bool M6PhraseIterator::Next(uint32& outDoc, float& outRank)
{
	bool result = false, done = false;
	float r;
	
	for (;;)
	{
		if (mIterators.empty())
			break;

		sort(mIterators.begin(), mIterators.end());

		outDoc = mIterators.back().mDoc;

		foreach (M6PhraseIteratorPart& part, mIterators)
		{
			while (part.mDoc < outDoc)
			{
				if (part.mIter->Next(part.mDoc, r))
					part.ReadArray();
				else
				{
					done = true;
					break;
				}
			}
		}

		if (mIterators.front().mDoc == outDoc)
		{
			result = true;
			outRank = 1;

			// now check the IDL vectors
			mIDLCache1 = mIterators.front().mIDL;
			
			for (auto i = mIterators.begin() + 1; i != mIterators.end(); ++i)
			{
				mIDLCache2.clear();

				std::set_intersection(mIDLCache1.begin(), mIDLCache1.end(),
					i->mIDL.begin(), i->mIDL.end(), std::back_inserter(mIDLCache2));
				
				if (mIDLCache2.empty())	// no adjacent words found
				{
					result = false;
					break;
				}
				
				swap(mIDLCache1, mIDLCache2);
			}

			foreach (M6PhraseIteratorPart& part, mIterators)
			{
				if (part.mIter->Next(part.mDoc, r))
					part.ReadArray();
				else
					done = true;
			}

			break;
		}

		if (mIterators.back().mIter->Next(mIterators.back().mDoc, r))
			mIterators.back().ReadArray();
		else
			done = true;
	}

	if (done)
	{
		foreach (M6PhraseIteratorPart& part, mIterators)
			delete part.mIter;
		mIterators.clear();
	}

	return result;
}
