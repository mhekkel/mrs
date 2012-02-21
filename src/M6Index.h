//	Basic index class using B+ tree data structure
//	
//	

#pragma once

#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/path.hpp>

#include "M6File.h"
#include "M6BitStream.h"

struct M6IndexImpl;
class M6CompressedArray;
union M6BitVector;
class M6Progress;

extern const uint32 kM6MaxKeyLength;

class M6BasicIndex
{
  public:
					M6BasicIndex(const boost::filesystem::path& inPath, MOpenMode inMode);

	virtual			~M6BasicIndex();

	void			Commit();
	void			Rollback();
	void			SetAutoCommit(bool inAutoCommit);
	void			SetBatchMode(bool inBatchMode);
	
	void			Vacuum(M6Progress& inProgress);
	
	virtual int		CompareKeys(const char* inKeyA, size_t inKeyLengthA,
						const char* inKeyB, size_t inKeyLengthB) const = 0;

	//// TODO: rewrite iterator to be able to mutate value's directly
	//class iterator : public std::iterator<std::forward_iterator_tag,const M6Tuple>
	//{
	//  public:
	//	typedef std::iterator<std::forward_iterator_tag, const M6Tuple>	base_type;
	//	typedef base_type::reference									reference;
	//	typedef base_type::pointer										pointer;
	//	
	//					iterator();
	//					iterator(const iterator& iter);
	//	iterator&		operator=(const iterator& iter);

	//	reference		operator*() const							{ return mCurrent; }
	//	pointer			operator->() const							{ return &mCurrent; }

	//	iterator&		operator++();
	//	iterator		operator++(int)								{ iterator iter(*this); operator++(); return iter; }

	//	bool			operator==(const iterator& iter) const		{ return mIndex == iter.mIndex and mPage == iter.mPage and mKeyNr == iter.mKeyNr; }
	//	bool			operator!=(const iterator& iter) const		{ return not operator==(iter); }

	//  private:
	//	friend struct M6IndexImpl;

	//					iterator(M6IndexImpl* inIndex, int64 inPageNr, uint32 inKeyNr);

	//	M6IndexImpl*	mIndex;
	//	int64			mPage;
	//	uint32			mKeyNr;
	//	M6Tuple			mCurrent;
	//};
	
//	// lame, but works for now (needed for boost::range)
//	typedef iterator const_iterator;
//	
////	iterator		begin() const;
////	iterator		end() const;
//	
//	iterator		lower_bound(const std::string& inKey) const;
//	iterator		upper_bound(const std::string& inKey) const;
	
	void			Insert(const std::string& inKey, uint32 inValue);
	void			Erase(const std::string& inKey);
	bool			Find(const std::string& inKey, uint32& outValue);

	uint32			size() const;
	uint32			depth() const;

	void			Dump() const;
	void			Validate() const;

  protected:
					M6BasicIndex(M6IndexImpl* inImpl);

	M6IndexImpl*	mImpl;
};

template<class INDEX, class COMPARATOR> class M6Index : public INDEX
{
  public:
	typedef COMPARATOR			M6Comparator;

					M6Index(const boost::filesystem::path& inPath, MOpenMode inMode)
						: INDEX(inPath, inMode) {}

//					M6Index(const boost::filesystem::path& inPath, M6SortedInputIterator& inData)
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

struct M6NumericComparator
{
	int operator()(const char* inKeyA, size_t inKeyLengthA, const char* inKeyB, size_t inKeyLengthB) const
	{
#pragma message("TODO improve numeric comparison")
		int d = 0;
		const char* ai = inKeyA + inKeyLengthA;
		const char* bi = inKeyB + inKeyLengthB;
		while (ai > inKeyA and bi > inKeyB)
		{
			--ai; --bi;
			if (*ai != *bi)
				d = *ai - *bi;
		}

		while (ai > inKeyA)
		{
			--ai;
			if (*ai != '0')
			{
				d = 1;
				break;
			}
		}

		while (bi > inKeyB)
		{
			--bi;
			if (*bi != '0')
			{
				d = -1;
				break;
			}
		}

		return d;
	}
};

typedef M6Index<M6BasicIndex, M6BasicComparator>	M6SimpleIndex;
typedef M6Index<M6BasicIndex, M6NumericComparator>	M6NumberIndex;

// --------------------------------------------------------------------

class M6MultiBasicIndex : public M6BasicIndex
{
  public:
					M6MultiBasicIndex(const boost::filesystem::path& inPath, MOpenMode inMode);

	void			Insert(const std::string& inKey, const std::vector<uint32>& inDocuments);
	bool			Find(const std::string& inKey, M6CompressedArray& outDocuments);
};

typedef M6Index<M6MultiBasicIndex, M6BasicComparator>	M6SimpleMultiIndex;
typedef M6Index<M6MultiBasicIndex, M6NumericComparator>	M6NumberMultiIndex;

// --------------------------------------------------------------------

class M6MultiIDLBasicIndex : public M6BasicIndex
{
  public:
					M6MultiIDLBasicIndex(const boost::filesystem::path& inPath, MOpenMode inMode);

	void			Insert(const std::string& inKey, int64 inIDLOffset, const std::vector<uint32>& inDocuments);
	bool			Find(const std::string& inKey, M6CompressedArray& outDocuments, int64& outIDLOffset);
};

typedef M6Index<M6MultiIDLBasicIndex, M6BasicComparator>	M6SimpleIDLMultiIndex;

// --------------------------------------------------------------------

class M6WeightedBasicIndex : public M6BasicIndex
{
  public:
					M6WeightedBasicIndex(const boost::filesystem::path& inPath, MOpenMode inMode);

	class M6WeightedIterator
	{
	  public:
						M6WeightedIterator();
						M6WeightedIterator(M6IndexImpl& inIndex, const M6BitVector& inBitVector, uint32 inCount);
						M6WeightedIterator(const M6WeightedIterator&);
		M6WeightedIterator&
						operator=(const M6WeightedIterator&);

		uint32			Size() const								{ return mSize; }
		bool			Next(uint32& outDocNr, uint8& outWeight);

	  private:
		M6IBitStream	mBits;
		std::vector<uint32>
						mDocs;
		uint32			mSize;
		uint8			mWeight;
	};
	
	void			Insert(const std::string& inKey, std::vector<std::pair<uint32,uint8>>& inDocuments);
	bool			Find(const std::string& inKey, M6WeightedIterator& outIterator);
	void			CalculateDocumentWeights(uint32 inDocCount, std::vector<float>& outWeights,
						M6Progress& inProgress);
};

typedef M6Index<M6WeightedBasicIndex, M6BasicComparator>	M6SimpleWeightedIndex;

