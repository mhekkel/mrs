//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <istream>
#include <boost/filesystem/path.hpp>

class M6Progress;

struct M6DataSourceImpl;

struct M6DataSource
{
  public:
	typedef boost::iostreams::filtering_stream<boost::iostreams::input> istream_type;

						M6DataSource(const boost::filesystem::path& inFile,
							M6Progress& inProgress);
	virtual				~M6DataSource();

	struct M6DataFile
	{
						M6DataFile() : mRefCount(1) {}
		
		std::string		mFilename;
		istream_type	mStream;
		uint32			mRefCount;
	};

	struct iterator : public std::iterator<std::forward_iterator_tag, M6DataFile>
	{
		typedef std::iterator<std::forward_iterator_tag, M6DataFile>	base_type;
		typedef base_type::reference									reference;
		typedef base_type::pointer										pointer;
		
						iterator() : mSource(nullptr), mDataFile(nullptr) {}
						iterator(M6DataSourceImpl* inSource);
						iterator(const iterator& iter);
		iterator&		operator=(const iterator& iter);
						~iterator();
	
		reference		operator*() 								{ return *mDataFile; }
		pointer			operator->() 								{ return mDataFile; }
	
		iterator&		operator++();					
		iterator		operator++(int)
							{ iterator iter(*this); operator++(); return iter; }
	
		bool			operator==(const iterator& iter) const
							{ return mSource == iter.mSource and mDataFile == iter.mDataFile; }
						
		bool			operator!=(const iterator& iter) const		{ return not operator==(iter); }
	
	  private:
		M6DataSourceImpl* mSource;
		M6DataFile*		mDataFile;
	};
	
	iterator			begin()										{ return mImpl ? iterator(mImpl): iterator(); }
	iterator			end()										{ return iterator(); }

  private:
	M6DataSourceImpl*	mImpl;
};
