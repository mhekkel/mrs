//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

//	Basic index class using B+ tree data structure
//	
//	

#pragma once

#include <boost/function.hpp>
#include <boost/filesystem/path.hpp>

#include "M6File.h"
#include "M6BitStream.h"
#include "M6Iterator.h"

struct M6IndexImpl;
class M6CompressedArray;
union M6BitVector;
class M6Progress;
class M6Lexicon;

class M6DuplicateKeyException : public std::exception
{
  public:
						M6DuplicateKeyException(const std::string& inKey);
	virtual const char*	what() const throw()	{ return mMessage; }
	
  private:
	char mMessage[80];
};

extern const uint32 kM6MaxKeyLength;

class M6BasicIndex
{
  public:
	virtual			~M6BasicIndex();

	static M6BasicIndex*
					Load(const boost::filesystem::path& inPath);

	virtual M6IndexType
					GetIndexType() const;

	void			Commit();
	void			Rollback();
	void			SetAutoCommit(bool inAutoCommit);

	void			SetBatchMode(M6Lexicon& inLexicon);
	void			FinishBatchMode(M6Progress& inProgress,
						std::exception_ptr& outException);
	bool			IsInBatchMode();
	
	void			Vacuum(M6Progress& inProgress);
	
	virtual int		CompareKeys(const char* inKeyA, size_t inKeyLengthA,
						const char* inKeyB, size_t inKeyLengthB) const = 0;
	virtual std::string
					StringToKey(const std::string& s) = 0;
	virtual std::string
					KeyToString(const std::string& s) = 0;

	// iterator is used to iterate over the keys in an index.
	// To access the accompanying data, use the M6Iterator producing
	// method.

	class iterator : public std::iterator<std::forward_iterator_tag,const std::string>
	{
	  public:
		typedef std::iterator<std::forward_iterator_tag, const std::string>	base_type;
		typedef base_type::reference										reference;
		typedef base_type::pointer											pointer;
		
						iterator();
						iterator(const iterator& iter);
		iterator&		operator=(const iterator& iter);
	
		reference		operator*() const							{ return mCurrent; }
		pointer			operator->() const							{ return &mCurrent; }
	
		iterator&		operator++();
		iterator		operator++(int)								{ iterator iter(*this); operator++(); return iter; }
	
		bool			operator==(const iterator& iter) const		{ return mIndex == iter.mIndex and mPage == iter.mPage and mKeyNr == iter.mKeyNr; }
		bool			operator!=(const iterator& iter) const		{ return not operator==(iter); }

		M6Iterator*		GetDocuments() const;
		uint32			GetCount() const;
		uint32			page() const								{ return mPage; }
		uint32			keynr() const								{ return mKeyNr; }
	
	  private:
		friend struct M6IndexImpl;
	
						iterator(M6IndexImpl* inIndex, uint32 inPageNr, uint32 inKeyNr);
	
		M6IndexImpl*	mIndex;
		uint32			mPage;
		uint32			mKeyNr;
		std::string		mCurrent;
	};
	
	// lame, but works for now (needed for boost::range)
	typedef iterator const_iterator;
	
	iterator		begin() const;
	iterator		end() const;
	
//	iterator		lower_bound(const std::string& inKey) const;
//	iterator		upper_bound(const std::string& inKey) const;

	void			GetBrowseSections(const std::string& inFirst, const std::string& inLast,
						uint32 inNrOfSections, std::vector<std::pair<std::string,std::string>>& outSections);

	void			GetBrowseSections(uint32 inNrOfSections,
						std::vector<std::pair<std::string,std::string>>& outSections)
					{
						GetBrowseSections("", "", inNrOfSections, outSections);
					}
	void			GetBrowseEntries(const std::string& inFirst, const std::string& inLast,
						std::vector<std::string>& outEntries);
	
	typedef boost::function<bool(const char* inKey, uint32 inKeyLength, uint32 inCount)>	KeyVisitor;
	void			VisitKeys(KeyVisitor inVisitor);
	
	void			Insert(const std::string& inKey, uint32 inValue);
	void			Erase(const std::string& inKey);
	bool			Find(const std::string& inKey, uint32& outValue);
	bool			Contains(const std::string& inKey);
	
	// for batch mode only:
	void			Insert(uint32 inKey, uint32 inValue);

	M6Iterator*		Find(const std::string& inKey);
	void			Find(const std::string& inKey, M6QueryOperator inOperator, std::vector<bool>& outBitmap, uint32& outCount);
	void			Find(const std::string& inLowerBound, const std::string& inUpperBound, std::vector<bool>& outBitmap, uint32& outCount);
	void			FindPattern(const std::string& inPattern, std::vector<bool>& outBitmap, uint32& outCount);
	M6Iterator*		FindString(const std::string& inString);

	uint32			size() const;
	uint32			depth() const;

	void			Dump() const;
	void			Validate() const;

  protected:
					M6BasicIndex(const boost::filesystem::path& inPath,
						M6IndexType inIndexType, MOpenMode inMode);
					M6BasicIndex(M6IndexImpl* inImpl);

	M6IndexImpl*	mImpl;
};

template<class INDEX, class COMPARATOR, M6IndexType TYPE> class M6Index : public INDEX
{
  public:
	typedef COMPARATOR			M6Comparator;

	M6Index(const boost::filesystem::path& inPath, MOpenMode inMode)
		: INDEX(inPath, TYPE, inMode) {}

	virtual int CompareKeys(const char* inKeyA, size_t inKeyLengthA,
		const char* inKeyB, size_t inKeyLengthB) const
	{
		return mComparator(inKeyA, inKeyLengthA, inKeyB, inKeyLengthB);
	}

	virtual std::string StringToKey(const std::string& s)
	{
		return mComparator.StringToKey(s);
	}

	virtual std::string KeyToString(const std::string& s)
	{
		return mComparator.KeyToString(s);
	}

protected:
	M6Comparator	mComparator;
};

// simplistic comparator, based on memcmp
struct M6BasicComparator
{
	int operator()(const char* inKeyA, size_t inKeyLengthA, const char* inKeyB, size_t inKeyLengthB) const
	{
		size_t l = inKeyLengthA;
		if (l > inKeyLengthB)
			l = inKeyLengthB;
		int d = memcmp(inKeyA, inKeyB, l);
		if (d == 0)
			d = static_cast<int>(inKeyLengthA - inKeyLengthB);
		return d;
	}

	std::string StringToKey(const std::string& key) { return key; }
	std::string KeyToString(const std::string& key) { return key; }
};

struct M6NumericComparator
{
	int operator()(const char* inKeyA, size_t inKeyLengthA, const char* inKeyB, size_t inKeyLengthB) const;

	std::string StringToKey(const std::string& key) { return key; }
	std::string KeyToString(const std::string& key) { return key; }
};

struct M6FloatComparator
{
	int operator()(const char* inKeyA, size_t inKeyLengthA, const char* inKeyB, size_t inKeyLengthB) const;

	std::string StringToKey(const std::string& key);
	std::string KeyToString(const std::string& key);
};

typedef M6Index<M6BasicIndex, M6BasicComparator, eM6CharIndex>	M6SimpleIndex;
typedef M6Index<M6BasicIndex, M6NumericComparator, eM6NumberIndex>	M6NumberIndex;
typedef M6Index<M6BasicIndex, M6FloatComparator, eM6FloatIndex>	M6FloatIndex;

// --------------------------------------------------------------------

class M6MultiBasicIndex : public M6BasicIndex
{
  public:
					M6MultiBasicIndex(const boost::filesystem::path& inPath, M6IndexType inIndexType, MOpenMode inMode);

	void			Insert(const std::string& inKey, const std::vector<uint32>& inDocuments);
	void			Insert(double inKey, const std::vector<uint32>& inDocuments);
//	bool			Find(const std::string& inKey, M6CompressedArray& outDocuments);

	// for batch mode only:
	void			Insert(uint32 inKey, const std::vector<uint32>& inDocuments);
};

typedef M6Index<M6MultiBasicIndex, M6BasicComparator, eM6CharMultiIndex> M6SimpleMultiIndex;
typedef M6Index<M6MultiBasicIndex, M6NumericComparator, eM6NumberMultiIndex> M6NumberMultiIndex;
typedef M6Index<M6MultiBasicIndex, M6FloatComparator, eM6FloatMultiIndex> M6FloatMultiIndex;

// --------------------------------------------------------------------

class M6MultiIDLBasicIndex : public M6BasicIndex
{
  public:
					M6MultiIDLBasicIndex(const boost::filesystem::path& inPath, M6IndexType inIndexType, MOpenMode inMode);

	void			Insert(const std::string& inKey, int64 inIDLOffset, const std::vector<uint32>& inDocuments);
//	bool			Find(const std::string& inKey, M6CompressedArray& outDocuments, int64& outIDLOffset);

	// for batch mode only:
	void			Insert(uint32 inKey, int64 inIDLOffset, const std::vector<uint32>& inDocuments);
};

typedef M6Index<M6MultiIDLBasicIndex, M6BasicComparator, eM6CharMultiIDLIndex> M6SimpleIDLMultiIndex;

// --------------------------------------------------------------------

struct M6MultiData;

class M6WeightedBasicIndex : public M6BasicIndex
{
  public:
					M6WeightedBasicIndex(const boost::filesystem::path& inPath, M6IndexType inIndexType, MOpenMode inMode);

	class M6WeightedIterator
	{
	  public:
						M6WeightedIterator();
						M6WeightedIterator(M6IndexImpl& inIndex, const M6BitVector& inBitVector, uint32 inCount, uint32 inMaxWeight);
						M6WeightedIterator(const M6WeightedIterator&);
						M6WeightedIterator(M6WeightedIterator&&);
		M6WeightedIterator&
						operator=(const M6WeightedIterator&);
		M6WeightedIterator&
						operator=(M6WeightedIterator&&);

		bool			Next(uint32& outDocNr, uint8& outWeight);

		uint32			GetCount() const								{ return mCount; }

	  private:
		M6IBitStream	mBits;
		std::vector<uint32>
						mDocs;
		uint32			mCount;
		uint8			mWeight;
	};
	
	void			SetMaxWeight(uint32 inMaxWeight);
	uint32			GetMaxWeight() const;
	
	void			Insert(const std::string& inKey, std::vector<std::pair<uint32,uint8>>& inDocuments);
	bool			Find(const std::string& inKey, M6WeightedIterator& outIterator);
	void			CalculateDocumentWeights(uint32 inDocCount, std::vector<float>& outWeights,
						M6Progress& inProgress);

	// for batch mode only:
	void			Insert(uint32 inKey, std::vector<std::pair<uint32,uint8>>& inDocuments);
	
  private:
	void			StoreDocumentBits(std::vector<std::pair<uint32,uint8>>& inDocuments, M6MultiData& data);
	
	uint32			mMaxWeight;
};

typedef M6Index<M6WeightedBasicIndex, M6BasicComparator, eM6CharWeightedIndex>	M6SimpleWeightedIndex;

