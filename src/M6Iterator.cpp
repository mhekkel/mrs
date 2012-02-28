#include "M6Lib.h"

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

M6UnionIterator::M6UnionIterator()
{
}

M6UnionIterator::M6UnionIterator(M6Iterator* inA, M6Iterator* inB)
{
	AddIterator(inA);
	AddIterator(inB);
}

void M6UnionIterator::AddIterator(M6Iterator* inIter)
{
	M6IteratorPart a = { inA };
	if (inA->Next(a.mDoc, a.mRank))
	{
		mIterators.push_back(a);
		push_heap(mIterators.begin(), mIterators.end());
	}
	else
		delete inA;
}

bool M6UnionIterator::Next(uint32& outDoc, float& outRank)
{
	bool result = false;

	while (not mIterators.empty())
	{
		pop_heap(mIterators.begin(), mIterators.end());
		
		outDoc = mIterators.back().mDoc;
		outRank = 1.0f;
		result = true;
		
		for (;;)
		{
			uint32 d;
			float r;

			if (mIterators.back().mIter->Next(d, r))
			{
				mIterators.back().mDoc = d;
				push_heap(mIterators.begin(), mIterators.end());
				
				if (mIterators.front().mDoc > outDoc)
					break;
			}
			else
			{
				delete mIterators.back().mIter;
				mIterators.pop_back();

				if (mIterators.empty())
					break;
			}
			
			pop_heap(mIterators.begin(), mIterators.end());
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
//	else
//		result = new M6UnionIterator(inA, inB);
	return result;
}

// --------------------------------------------------------------------

M6Iterator* M6IntersectionIterator::Create(M6Iterator* inA, M6Iterator* inB)
{
	M6Iterator* result;
	if (inA == nullptr)
		result = inB;
	else if (inB == nullptr)
		result = inA;
//	else
//		result = new M6IntersectionIterator(inA, inB);
	return result;
}
