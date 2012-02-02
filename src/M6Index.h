//	Basic index class using B+ tree data structure
//	
//	

#pragma once

#include <boost/function.hpp>

#include "M6File.h"

class M6IndexImpl;

extern const uint32 kM6MaxKeyLength;

struct M6Tuple
{
	std::string		key;
	int64			value;

					M6Tuple() : value(0) {}
					M6Tuple(const std::string& inKey, int64 inValue)
						: key(inKey), value(inValue) {}
};

typedef boost::function<bool(M6Tuple&)>		M6SortedInputIterator;

class M6BasicIndex
{
  public:
					M6BasicIndex(const std::string& inPath, MOpenMode inMode);
					M6BasicIndex(const std::string& inPath, M6SortedInputIterator& inData);

	virtual			~M6BasicIndex();

	void			Commit();
	void			Rollback();
	void			SetAutoCommit(bool inAutoCommit);
	
	void			Vacuum();
	
	virtual int		CompareKeys(const char* inKeyA, size_t inKeyLengthA,
						const char* inKeyB, size_t inKeyLengthB) const = 0;

	// TODO: rewrite iterator to be able to mutate value's directly
	class iterator : public std::iterator<std::forward_iterator_tag,const M6Tuple>
	{
	  public:
		typedef std::iterator<std::forward_iterator_tag, const M6Tuple>	base_type;
		typedef base_type::reference									reference;
		typedef base_type::pointer										pointer;
		
						iterator();
						iterator(const iterator& iter);
		iterator&		operator=(const iterator& iter);

		reference		operator*() const							{ return mCurrent; }
		pointer			operator->() const							{ return &mCurrent; }

		iterator&		operator++();
		iterator		operator++(int)								{ iterator iter(*this); operator++(); return iter; }

		bool			operator==(const iterator& iter) const		{ return mIndex == iter.mIndex and mPage == iter.mPage and mKeyNr == iter.mKeyNr; }
		bool			operator!=(const iterator& iter) const		{ return not operator==(iter); }

	  private:
		friend class M6IndexImpl;

						iterator(M6IndexImpl* inIndex, int64 inPageNr, uint32 inKeyNr);

		M6IndexImpl*	mIndex;
		int64			mPage;
		uint32			mKeyNr;
		M6Tuple			mCurrent;
	};
	
	// lame, but works for now (needed for boost::range)
	typedef iterator const_iterator;
	
	iterator		begin() const;
	iterator		end() const;
	
	iterator		lower_bound(const std::string& inKey) const;
	iterator		upper_bound(const std::string& inKey) const;
	
	void			insert(const std::string& inKey, int64 inValue);
	void			erase(const std::string& inKey);
	bool			find(const std::string& inKey, int64& outValue);

	uint32			size() const;
	uint32			depth() const;

#if DEBUG
	void			dump() const;
	void			validate() const;
#endif

  protected:
					M6BasicIndex(M6IndexImpl* inImpl);

	M6IndexImpl*	mImpl;
};

template<class INDEX, class COMPARATOR> class M6Index : public INDEX
{
  public:
	typedef COMPARATOR			M6Comparator;

					M6Index(const std::string& inPath, MOpenMode inMode)
						: INDEX(inPath, inMode) {}

//					M6Index(const std::string& inPath, M6SortedInputIterator& inData)
//						: INDEX(inPath, inData) {}

	virtual int		CompareKeys(const char* inKeyA, size_t inKeyLengthA,
								const char* inKeyB, size_t inKeyLengthB) const
						{ return mComparator(inKeyA, inKeyLengthA, inKeyB, inKeyLengthB); }

  protected:
	M6Comparator	mComparator;
};

// simplistic comparator, based on strncmp
struct M6BasicComparator
{
	int operator()(const char* inKeyA, size_t inKeyLengthA, const char* inKeyB, size_t inKeyLengthB) const
	{
		size_t l = inKeyLengthA;
		if (l > inKeyLengthB)
			l = inKeyLengthB;
		int d = strncmp(inKeyA, inKeyB, l);
		if (d == 0)
			d = static_cast<int>(inKeyLengthA - inKeyLengthB);
		return d;
	}
};

typedef M6Index<M6BasicIndex, M6BasicComparator>	M6SimpleIndex;

// --------------------------------------------------------------------

class M6MultiBasicIndex : public M6BasicIndex
{
  public:
					M6MultiBasicIndex(const std::string& inPath, MOpenMode inMode);

	class multi_iterator
	{
	  public:
		uint32		size() const;
		bool		next(uint32& outDocNr);
	};

	void			Insert(const std::string& inKey, const std::vector<uint32>& inDocuments);
	bool			Find(const std::string& inKey, iterator& outIterator);
};

typedef M6Index<M6MultiBasicIndex, M6BasicComparator>	M6SimpleMultiIndex;

// --------------------------------------------------------------------

class M6WeightedBasicIndex : public M6BasicIndex
{
  public:
					M6WeightedBasicIndex(const std::string& inPath, MOpenMode inMode);

	class weighted_iterator
	{
	  public:
		uint32		size() const;
		float		idf_correction(uint32 inDocCount) const;
		bool		next(uint32& outDocNr, uint8& outFrequency);
	};
	
	void			Insert(const std::string& inKey, const std::vector<std::pair<uint32,uint8>>& inDocuments);
	bool			Find(const std::string& inKey, weighted_iterator& outIterator);
};

typedef M6Index<M6WeightedBasicIndex, M6BasicComparator>	M6SimpleWeightedIndex;

