#pragma once

#include <iterator>
#include <boost/iostreams/filtering_stream.hpp>

#include "M6File.h"

class M6DocStoreImpl;

class M6DocStore
{
  public:
					M6DocStore(const boost::filesystem::path& inPath, MOpenMode inMode);
	virtual			~M6DocStore();
	
	void			GetInfo(uint32& outDocCount, int64& outFileSize, int64& outRawSize);
	
	uint32			StoreDocument(const char* inData, size_t inSize, size_t inRawSize);
	bool			FetchDocument(uint32 inDocNr, uint32& outPageNr, uint32& outDocSize);
	void			OpenDataStream(uint32 inDocNr, uint32 inPageNr, uint32 inDocSize,
						boost::iostreams::filtering_stream<boost::iostreams::input>& ioStream);
	void			EraseDocument(uint32 inDocNr);

	uint8			RegisterAttribute(const std::string& inName);
	std::string		GetAttributeName(uint8 inAttrNr) const;

	uint32			size() const;
	uint32			NextDocumentNumber() const;

	void			Commit();

	void			Validate();
	void			Dump();

  protected:
	M6DocStoreImpl*	mImpl;
};
