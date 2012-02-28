#pragma once

#include <queue>

// M6Iterator is a base class to iterate over query results

class M6Iterator
{
  public:
					M6Iterator() {}
					~M6Iterator() {}

	virtual bool	Next(uint32& outDoc, float& outRank) = 0;

	static void		Intersect(std::vector<uint32>& ioDocs, M6Iterator* inIterator);

  private:
					M6Iterator(const M6Iterator&);
	M6Iterator&		operator=(const M6Iterator&);
};

// --------------------------------------------------------------------
// There are many implementations of M6Iterator:

class M6SingleDocIterator : public M6Iterator
{
  public:
					M6SingleDocIterator(uint32 inDoc, float inRank = 1.0f)
						: mDoc(inDoc), mRank(inRank) {}

	virtual bool	Next(uint32& outDoc, float& outRank)
					{
						outDoc = mDoc;
						mDoc = 0;
						outRank = mRank;
						return outDoc != 0;
					}

  private:
	uint32			mDoc;
	float			mRank;
};

//typedef std::vector<M6Iterator*>	M6IteratorList;
//struct M6CompareIterators
//{
//	bool operator()(const M6Iterator* a, const M6Iterator* b) const
//			{ return a->GetCurrent() > b->GetCurrent(); }
//};

class M6UnionIterator : public M6Iterator
{
  public:
					M6UnionIterator();
					M6UnionIterator(M6Iterator* inA, M6Iterator* inB);

	void			AddIterator(M6Iterator* inIter);

	virtual bool	Next(uint32& outDoc, float& outRank);

	static M6Iterator*
					Create(M6Iterator* inA, M6Iterator* inB);

 // private:
	//M6IteratorList	mIterators;
};

class M6IntersectionIterator : public M6Iterator
{
  public:
					M6IntersectionIterator();
					M6IntersectionIterator(M6Iterator* inA, M6Iterator* inB);

	void			AddIterator(M6Iterator* inIter);

	virtual bool	Next(uint32& outDoc, float& outRank);

	static M6Iterator*
					Create(M6Iterator* inA, M6Iterator* inB);

 // private:
	//M6IteratorList	mIterators;
};

class M6VectorIterator : public M6Iterator
{
  public:
	typedef std::vector<std::pair<uint32,float>>	M6Vector;

					M6VectorIterator(M6Vector& inVector)
					{
						mVector.swap(inVector);
						mPtr = mVector.begin();
					}

	virtual bool	Next(uint32& outDoc, float& outRank)
					{
						bool result = false;
						if (mPtr != mVector.end())
						{
							outDoc = mPtr->first;
							outRank = mPtr->second;
							++mPtr;
							result = true;
						}
						return result;
					}

  private:
	M6Vector		mVector;
	M6Vector::iterator
					mPtr;
};
