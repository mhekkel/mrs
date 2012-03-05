#pragma once

#include <vector>

#include "M6File.h"

class M6Document;
class M6DatabankImpl;
class M6DocStore;
class M6Lexicon;
class M6Iterator;

struct M6IndexInfo
{
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
	M6IndexInfoList	mIndexInfo;
};

class M6Databank
{
  public:
					M6Databank(const boost::filesystem::path& inPath,
						MOpenMode inMode);
	virtual			~M6Databank();

	static M6Databank*
					CreateNew(const boost::filesystem::path& inPath);

	void			GetInfo(M6DatabankInfo& outInfo);

	void			StartBatchImport(M6Lexicon& inLexicon);
	void			CommitBatchImport();
	
	void			RecalculateDocumentWeights();
	void			Vacuum();

	void			Validate();
	void			DumpIndex(const std::string& inIndex, std::ostream& inStream);
	
	void			Store(M6Document* inDocument);
	M6Document*		Fetch(uint32 inDocNr);
	
	// high-level interface
	M6Iterator*		Find(const std::string& inQuery, bool inAllTermsRequired,
						uint32 inReportLimit);
	
	// low-level interface
	M6Iterator*		Find(const std::string& inIndex, const std::string& inTerm,
						bool inTermIsPattern);
	M6Iterator*		FindString(const std::string& inIndex, const std::string& inString);
	
	M6DocStore&		GetDocStore();
	
	uint32			size() const;
	uint32			GetMaxDocNr() const;

  private:

	M6DatabankImpl*	mImpl;
};
