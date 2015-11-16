//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <vector>
#include <algorithm>
#include <tuple>

#include <boost/filesystem/path.hpp>

#include "M6BitStream.h"
#include "M6File.h"

// --------------------------------------------------------------------
// M6Iterator is a base class to iterate over query results

class M6Iterator
{
  public:
					M6Iterator() : mCount(0), mRanked(false) {}
	virtual 		~M6Iterator() {}

	virtual bool	Next(uint32& outDoc, float& outRank) = 0;

	static void		Intersect(std::vector<uint32>& ioDocs, M6Iterator* inIterator);

	// count is a heuristic, it is a best guess, don't trust it!
	virtual uint32	GetCount() const				{ return mCount; }
	virtual void	SetCount(uint32 inCount)		{ mCount = inCount; }

	bool			IsRanked() const				{ return mRanked; }

  protected:
	uint32			mCount;
	bool			mRanked;

  private:
					M6Iterator(const M6Iterator&);
	M6Iterator&		operator=(const M6Iterator&);
};

// --------------------------------------------------------------------
// There are many implementations of M6Iterator:

class M6AllDocIterator : public M6Iterator
{
  public:
					M6AllDocIterator(uint32 inMax) : mCur(1), mMax(inMax)
					{
						mCount = mMax;
					}

	virtual bool	Next(uint32& outDoc, float& outRank)
					{
						outRank = 1.0f;
						outDoc = mCur++;
						return mCur <= mMax;
					}

  private:
	uint32			mCur, mMax;
};

class M6NoDocIterator
{
  public:
	virtual bool	Next(uint32&, float&) { return false; }
};

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

class M6MultiDocIterator : public M6Iterator
{
  public:
					M6MultiDocIterator(const M6IBitStream& inBits, uint32 inLength)
						: mIter(inBits, inLength)
					{
						mCount = inLength;
					}

					M6MultiDocIterator(M6IBitStream&& inBits, uint32 inLength)
						: mIter(std::move(inBits), inLength)
					{
						mCount = inLength;
					}

	virtual bool	Next(uint32& outDoc, float& outRank)
					{
						outRank = 1.0f;
						return mIter.Next(outDoc);
					}
	
  private:
	M6CompressedArrayIterator	mIter;
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
					M6UnionIterator(std::list<M6Iterator*> inIters);

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

class M6PhraseIterator : public M6Iterator
{
  public:

					M6PhraseIterator(boost::filesystem::path& inIDLFile,
						std::vector<std::tuple<M6Iterator*,int64,uint32>>& inIterators);
					~M6PhraseIterator();
	
	virtual bool	Next(uint32& outDoc, float& outRank);

  private:

	struct M6PhraseIteratorPart
	{
		M6Iterator*			mIter;
		M6IBitStream		mBits;
		uint32				mIndex;
		uint32				mDoc;
		std::vector<uint32>	mIDL;
		
		M6PhraseIteratorPart(M6Iterator* inIter, M6IBitStream&& inIBitStream, uint32 inIndex);
		M6PhraseIteratorPart(M6PhraseIteratorPart&& rhs);
		
		M6PhraseIteratorPart&
							operator=(M6PhraseIteratorPart&&);
	
		bool				operator>(const M6PhraseIteratorPart& inPart) const
								{ return mDoc > inPart.mDoc; }
		bool				operator<(const M6PhraseIteratorPart& inPart) const
								{ return mDoc < inPart.mDoc; }

		void				ReadArray();
	};

	typedef std::vector<M6PhraseIteratorPart> M6PhraseIteratorParts;

	M6PhraseIteratorParts	mIterators;
	M6File					mIDLFile;
	std::vector<uint32>		mIDLCache1, mIDLCache2;
};

class M6VectorIterator : public M6Iterator
{
  public:
	typedef std::vector<std::pair<uint32,float>>	M6Vector;

					M6VectorIterator(M6Vector& inVector)
					{
						std::swap(mVector, inVector);
						mPtr = mVector.begin();
						mCount = static_cast<uint32>(mVector.size());
						mRanked = true;
					}

					M6VectorIterator(std::vector<uint32>& inVector)
					{
						std::transform(inVector.begin(), inVector.end(), std::back_inserter(mVector),
							[](uint32 doc) -> std::pair<uint32,float> { return std::make_pair(doc, 1.0f); });
						mPtr = mVector.begin();
						mCount = static_cast<uint32>(mVector.size());
						mRanked = true;
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

class M6BitmapIterator : public M6Iterator
{
  public:
	typedef std::vector<bool>	M6Vector;

					M6BitmapIterator(M6Vector& inVector, uint32 inCount)
					{
						std::swap(mVector, inVector);
						mPtr = mVector.begin();
						mCount = inCount;
						mRanked = false;
					}

	virtual bool	Next(uint32& outDoc, float& outRank)
					{
						bool result = false;
						while (mPtr != mVector.end())
						{
							if (*mPtr++)
							{
								outDoc = static_cast<uint32>(mPtr - mVector.begin() - 1);
								outRank = 1.0f;
								result = true;
								break;
							}
						}
						return result;
					}

  private:
	M6Vector		mVector;
	M6Vector::iterator
					mPtr;
};
