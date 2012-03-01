#include "M6Lib.h"

#include <cassert>

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Iterator.h"

using namespace std;

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
			pop_heap(mIterators.begin(), mIterators.end());
			
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
	AddIterator(inA);
	AddIterator(inB);
}

M6IntersectionIterator::~M6IntersectionIterator()
{
	foreach (M6IteratorPart& part, mIterators)
		delete part.mIter;
	mIterators.clear();
}

void M6IntersectionIterator::AddIterator(M6Iterator* inIter)
{
	M6IteratorPart p = { inIter };
	
	float r;
	if (inIter->Next(p.mDoc, r))
		mIterators.push_back(p);
	else
		delete inIter;
}

bool M6IntersectionIterator::Next(uint32& outDoc, float& outRank)
{
	bool result = false, done = mIterators.empty();
	float r;
	
	while (not (result or done))
	{
		sort(mIterators.begin(), mIterators.end());

		outDoc = mIterators.back().mDoc;
		if (mIterators.front().mDoc == outDoc)
		{
			result = true;
			foreach (M6IteratorPart& part, mIterators)
				done = done or part.mIter->Next(part.mDoc, r) == false;
			break;
		}
		
		foreach (M6IteratorPart& part, mIterators)
		{
			for (;;)
			{
				if (not part.mIter->Next(part.mDoc, r))
				{
					done = true;
					break;
				}
				
				if (part.mDoc >= outDoc)
					break;
			}
		}
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
	M6Iterator* result;
	if (inA == nullptr)
		result = inB;
	else if (inB == nullptr)
		result = inA;
	else if (dynamic_cast<M6IntersectionIterator*>(inA) != nullptr)
		static_cast<M6IntersectionIterator*>(inA)->AddIterator(inB);
	else if (dynamic_cast<M6IntersectionIterator*>(inB) != nullptr)
		static_cast<M6IntersectionIterator*>(inB)->AddIterator(inA);
	else
		result = new M6IntersectionIterator(inA, inB);
	return result;
}
