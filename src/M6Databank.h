#pragma once

#include <vector>
#include <boost/tr1/tuple.hpp>

#include "M6File.h"

class M6Document;
class M6DatabankImpl;
class M6DocStore;
class M6Lexicon;
class M6Iterator;

struct M6IndexInfo
{
#pragma message("unique??? data type???")
	std::string		mName;
	M6IndexType		mType;
	uint32			mCount;
	int64			mFileSize;
};
typedef std::vector<M6IndexInfo>	M6IndexInfoList;

struct M6DatabankInfo
{
	uint32			mDocCount;
	int64			mRawTextSize;
	int64			mDataStoreSize;
	int64			mTotalSize;
	std::string		mUUID;
	std::string		mVersion;
	std::string		mLastUpdate;
	boost::filesystem::path
					mDbDirectory;
	M6IndexInfoList	mIndexInfo;
};

class M6Databank
{
  public:
					// constructor that creates a read only object from an 
					// existing databank.
					M6Databank(const boost::filesystem::path& inPath, MOpenMode inMode = eReadOnly);
	virtual			~M6Databank();

	static M6Databank*
					CreateNew(const std::string& inDatabankID,
						const boost::filesystem::path& inPath, const std::string& inVersion);

	void			GetInfo(M6DatabankInfo& outInfo);
	std::string		GetUUID() const;

	void			StartBatchImport(M6Lexicon& inLexicon);
	void			EndBatchImport();
	void			FinishBatchImport();
	
	void			RecalculateDocumentWeights();
	void			Vacuum();

	void			Validate();
	void			DumpIndex(const std::string& inIndex, std::ostream& inStream);
	
	void			Store(M6Document* inDocument);
	void			StoreLink(const std::string& inDocID,
						const std::string& inLinkedDb, const std::string& inLinkedID);
	
	M6Document*		Fetch(uint32 inDocNr);
	M6Document*		Fetch(const std::string& inID);
	
	// high-level interface
	M6Iterator*		Find(const std::string& inQuery, bool inAllTermsRequired,
						uint32 inReportLimit);
	
	// low-level interface
	M6Iterator*		Find(const std::vector<std::string>& inQueryTerms,
						M6Iterator* inFilter, bool inAllTermsRequired, uint32 inReportLimit);
	M6Iterator*		Find(const std::string& inIndex, const std::string& inTerm,
						M6QueryOperator inOperator = eM6Equals);
	M6Iterator*		FindPattern(const std::string& inIndex, const std::string& inPattern);
	M6Iterator*		FindString(const std::string& inIndex, const std::string& inString);

	// Exist returns <documents exist,docnr for a unique match>
	std::tr1::tuple<bool,uint32>
					Exists(const std::string& inIndex, const std::string& inValue);
	
	// dictionary interface
	void			SuggestCorrection(const std::string& inWord,
						std::vector<std::pair<std::string,uint16>>& outCorrections);
	void			SuggestSearchTerms(const std::string& inWord,
						std::vector<std::string>& outSearchTerms);

	M6DocStore&		GetDocStore();
	
	uint32			size() const;
	uint32			GetMaxDocNr() const;
	
  private:
					// private constructor to create a new databank
					M6Databank(const std::string& inDatabankID, const boost::filesystem::path& inPath,
						const std::string& inVersion);

	M6DatabankImpl*	mImpl;
};
