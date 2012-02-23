#pragma once

#include <queue>

// M6Iterator is a base class to iterate over query results

struct M6CurrentDoc
{
	uint32			mDocNr;
	uint8			mDocWeight;
	
	bool			operator<(const M6CurrentDoc& inDoc) const	{ return mDocNr < inDoc.mDocNr; }
	bool			operator>(const M6CurrentDoc& inDoc) const	{ return mDocNr > inDoc.mDocNr; }
	bool			operator==(const M6CurrentDoc& inDoc) const	{ return mDocNr == inDoc.mDocNr; }
	bool			operator!=(const M6CurrentDoc& inDoc) const	{ return mDocNr != inDoc.mDocNr; }
};

class M6Iterator
{
  public:
					M6Iterator();
					~M6Iterator();

	virtual bool	Next() = 0;

	const M6CurrentDoc&	operator*() const		{ return mCurrent; }
	const M6CurrentDoc* operator->() const		{ return &mCurrent; }
	
	const M6CurrentDoc& GetCurrent() const		{ return mCurrent; }

  protected:
	M6CurrentDoc	mCurrent;

  private:
					M6Iterator(const M6Iterator&);
	M6Iterator&		operator=(const M6Iterator&);
};

typedef std::vector<M6Iterator*>	M6IteratorList;
struct M6CompareIterators
{
	bool operator()(const M6Iterator* a, const M6Iterator* b) const
			{ return a->GetCurrent() > b->GetCurrent(); }
};

class M6UnionIterator : public M6Iterator
{
  public:
					M6UnionIterator();

	template<class InputIterator>
					M6UnionIterator(InputIterator inFirst, InputIterator inLast);

	void			AddIterator(M6Iterator* inIter);

	virtual bool	Next();

  private:
	M6IteratorList	mIterators;
};

class M6IntersectionIterator : public M6Iterator
{
  public:
					M6IntersectionIterator();

	template<class InputIterator>
					M6IntersectionIterator(InputIterator inFirst, InputIterator inLast);

	void			AddIterator(M6Iterator* inIter);

	virtual bool	Next();

  private:
	M6IteratorList	mIterators;
};

