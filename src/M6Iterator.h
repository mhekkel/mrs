#pragma once

#include <vector>
#include <algorithm>

// --------------------------------------------------------------------
// M6Iterator is a base class to iterate over query results

class M6Iterator
{
  public:
					M6Iterator() : mCount(0) {}
					~M6Iterator() {}

	virtual bool	Next(uint32& outDoc, float& outRank) = 0;

	static void		Intersect(std::vector<uint32>& ioDocs, M6Iterator* inIterator);

	// count is a heuristic, it is a best guess, don't trust it!
	virtual uint32	GetCount() const				{ return mCount; }
	virtual void	SetCount(uint32 inCount)		{ mCount = inCount; }

  protected:
	uint32			mCount;

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
						: mDoc(inDoc), mRank(inRank) { mCount = 1; }

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

class M6NotIterator : public M6Iterator
{
  public:
					M6NotIterator(M6Iterator* inIter, uint32 inMax);
					~M6NotIterator() { delete mIter; }

	virtual bool	Next(uint32& outDoc, float& outRank);

  private:
	M6Iterator*		mIter;
	uint32			mCur, mNext, mMax;
};

// --------------------------------------------------------------------
//	Unions and intersections use the same 'container'

struct M6IteratorPart
{
	M6Iterator*		mIter;
	uint32			mDoc;

	bool			operator>(const M6IteratorPart& inPart) const
						{ return mDoc > inPart.mDoc; }
	bool			operator<(const M6IteratorPart& inPart) const
						{ return mDoc < inPart.mDoc; }
};
typedef std::vector<M6IteratorPart> M6IteratorParts;

class M6UnionIterator : public M6Iterator
{
  public:
					M6UnionIterator();
					~M6UnionIterator();
					M6UnionIterator(M6Iterator* inA, M6Iterator* inB);

	void			AddIterator(M6Iterator* inIter);

	virtual bool	Next(uint32& outDoc, float& outRank);

	static M6Iterator*
					Create(M6Iterator* inA, M6Iterator* inB);

  private:
	M6IteratorParts	mIterators;
};

class M6IntersectionIterator : public M6Iterator
{
  public:
					M6IntersectionIterator();
					~M6IntersectionIterator();
					M6IntersectionIterator(M6Iterator* inA, M6Iterator* inB);

	void			AddIterator(M6Iterator* inIter);

	virtual bool	Next(uint32& outDoc, float& outRank);

	static M6Iterator*
					Create(M6Iterator* inA, M6Iterator* inB);

  private:

	M6IteratorParts	mIterators;
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

					M6VectorIterator(std::vector<uint32>& inVector)
					{
						std::transform(inVector.begin(), inVector.end(), std::back_inserter(mVector),
							[](uint32 doc) -> std::pair<uint32,float> { return std::make_pair(doc, 1.0f); });
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
