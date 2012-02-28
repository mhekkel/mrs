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
