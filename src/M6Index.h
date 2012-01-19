#pragma once

#include <boost/function.hpp>

class M6Index;

class M6IndexImpl;
struct M6Tuple
{
	std::string		key;
	int64			value;
};

class M6IndexBase
{
  public:

					M6IndexBase(const std::string& inPath, bool inCreate);
	virtual			~M6IndexBase();
	
//	typedef boost::function<bool(std::string&,int64&)>	MDataProviderFunc;
//	
//	M6IndexBase*	Create(const fs::path& inPath, const std::string& inLocale,
//						bool inCaseSensitive, MDataProviderFunc& inData);
//
	virtual int		CompareKeys(const std::string& inKeyA, const std::string& inKeyB) const;

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
	iterator		Insert(const std::string& key, int64 value);
//	iterator		insert(iterator before, const M6Tuple& value);

	uint32			Size() const;

  protected:
	M6IndexImpl*	mImpl;
};

//template
//<
//	typename Comparator
//>
//class M6Index : public M6IndexBase
//{
//  public:
//
//	virtual int		CompareKeys(const std::string& inKeyA, const std::string& inKeyB) const;
//	
//};
