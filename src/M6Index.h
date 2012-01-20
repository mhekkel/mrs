//	Basic index class
//	
//	

#pragma once

#include <boost/function.hpp>

class M6IndexImpl;

struct M6Tuple
{
	std::string		key;
	int64			value;
};

class M6BasicIndex
{
  public:

					M6BasicIndex(const std::string& inPath, bool inCreate);
	virtual			~M6BasicIndex();
	
//	typedef boost::function<bool(std::string&,int64&)>	MDataProviderFunc;
//	
//	M6BasicIndex*	Create(const fs::path& inPath, const std::string& inLocale,
//						bool inCaseSensitive, MDataProviderFunc& inData);
//
	virtual int		CompareKeys(const char* inKeyA, uint32 inKeyLengthA,
						const char* inKeyB, uint32 inKeyLengthB) const = 0;

	class iterator : public std::iterator<std::bidirectional_iterator_tag,M6Tuple>
	{
	  public:
		typedef std::iterator<std::bidirectional_iterator_tag, const M6Tuple>	base_type;
		typedef base_type::reference											reference;
		typedef base_type::pointer												pointer;
		
						iterator();
						iterator(const iterator& other);
						iterator(M6IndexImpl* inIndex, uint32 inPageNr, uint32 inKeyNr);

		iterator&		operator=(const iterator& other);

		reference		operator*();
		pointer			operator->() const;

		iterator&		operator++();
		iterator		operator++(int)								{ iterator iter(*this); operator++(); return iter; }

		iterator&		operator--();
		iterator		operator--(int)								{ iterator iter(*this); operator++(); return iter; }
		bool			operator==(const iterator& other) const;
		bool			operator!=(const iterator& other) const;

	  private:
		M6IndexImpl*	mIndex;
		uint32			mPage;
		uint32			mKeyNr;
	};
	
	iterator		Begin() const;
	iterator		End() const;
	
	iterator		LowerBound(const std::string& inKey) const;
	iterator		UpperBound(const std::string& inKey) const;
	
//	iterator		Insert(const M6Tuple& value);
	void			Insert(const std::string& key, int64 value);
//	iterator		insert(iterator before, const M6Tuple& value);

	uint32			Size() const;

  protected:
	M6IndexImpl*	mImpl;
};

template<class COMPARATOR> class M6Index : public M6BasicIndex
{
  public:
	typedef COMPARATOR			M6Comparator;

					M6Index(const std::string& inPath, bool inCreate)
						: M6BasicIndex(inPath, inCreate) {}

	virtual int		CompareKeys(const char* inKeyA, uint32 inKeyLengthA,
								const char* inKeyB, uint32 inKeyLengthB) const
						{ return mComparator(inKeyA, inKeyLengthA, inKeyB, inKeyLengthB); }

  protected:
	M6Comparator	mComparator;
};

// simplistic comparator, based on strncmp
struct M6BasicComparator
{
	int operator()(const char* inKeyA, uint32 inKeyLengthA, const char* inKeyB, uint32 inKeyLengthB) const
	{
		uint32 l = inKeyLengthA;
		if (l > inKeyLengthB)
			l = inKeyLengthB;
		int d = strncmp(inKeyA, inKeyB, l);
		if (d == 0)
			d = int(inKeyLengthA - inKeyLengthB);
		return d;
	}
};

typedef M6Index<M6BasicComparator>	M6SimpleIndex;
