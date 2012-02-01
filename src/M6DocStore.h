#pragma once

#include <iterator>

#include "M6File.h"

class M6Document;
class M6DocStoreImpl;

class M6DocStore
{
  public:
					M6DocStore(const std::string& inPath, MOpenMode inMode);
	virtual			~M6DocStore();
	
	uint32			StoreDocument(M6Document* inDocument);
	void			EraseDocument(uint32 inDocNr);
	bool			FetchDocument(uint32 inDocNr, M6Document& outDocument);

	class iterator : public std::iterator<std::forward_iterator_tag, const M6Document>
	{
	  public:
		typedef std::iterator<std::forward_iterator_tag, const M6Document>	base_type;
		typedef base_type::reference										reference;
		typedef base_type::pointer											pointer;
		
						iterator();
						iterator(const iterator& iter);
		iterator&		operator=(const iterator& iter);

		reference		operator*() const							{ return *mCurrent; }
		pointer			operator->() const							{ return mCurrent; }

		iterator&		operator++();
		iterator		operator++(int)								{ iterator iter(*this); operator++(); return iter; }

		bool			operator==(const iterator& iter) const		{ return mStore == iter.mStore and mPage == iter.mPage and mDocIx == iter.mDocIx; }
		bool			operator!=(const iterator& iter) const		{ return not operator==(iter); }

	  private:
		friend class M6IndexImpl;

						iterator(M6DocStoreImpl* inStore, int64 inPageNr, uint32 inDocIx);

		M6DocStoreImpl*	mStore;
		uint32			mPage;
		uint32			mDocIx;
		M6Document*		mCurrent;
	};

	typedef iterator const_iterator;
	
	iterator			begin() const;
	iterator			end() const;

	uint32				size() const;

  protected:
	M6DocStoreImpl*		mImpl;
};
