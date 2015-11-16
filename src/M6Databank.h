//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <vector>
#include <map>
#include <set>
#include <tuple>

#include "M6File.h"

class M6Databank;
class M6Document;
class M6DatabankImpl;
class M6DocStore;
class M6Lexicon;
class M6Iterator;
class M6BasicIndex;

typedef boost::shared_ptr<M6BasicIndex> M6BasicIndexPtr;

// A link mesh contains a mapping from db ID's and aliases to
// actual databank objects.
typedef std::map<std::string,std::set<M6Databank*>> M6LinkMap;

struct M6IndexInfo
{	// TODO: add unique flag here
	std::string		mName;
	std::string		mDesc;
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
						const boost::filesystem::path& inPath, const std::string& inVersion,
						const std::vector<std::pair<std::string,std::string>>& inIndexNames);

	void			GetInfo(M6DatabankInfo& outInfo);
	std::string		GetUUID() const;
	boost::filesystem::path
					GetDbDirectory() const;

	void			StartBatchImport(M6Lexicon& inLexicon);
	void			EndBatchImport();
	void			FinishBatchImport();
	
	void			RecalculateDocumentWeights();
	void			Vacuum();

	void			Validate();
	void			DumpIndex(const std::string& inIndex, std::ostream& inStream);
	
	void			Store(M6Document* inDocument);
	
	M6Document*		Fetch(uint32 inDocNr);
	M6Document*		Fetch(const std::string& inID);
	
	// high-level interface
	M6Iterator*		Find(const std::string& inQuery, bool inAllTermsRequired,
						uint32 inReportLimit);
	M6Iterator*		FindBoolean(const std::string& inQuery, uint32 inReportLimit);
	
	// low-level interface
	M6Iterator*		Find(const std::vector<std::string>& inQueryTerms,
						M6Iterator* inFilter, bool inAllTermsRequired, uint32 inReportLimit);
	M6Iterator*		Find(const std::string& inIndex, const std::string& inTerm,
						M6QueryOperator inOperator = eM6Equals);
	M6Iterator*		Find(const std::string& inIndex, const std::string& inLowerBound,
						const std::string& inUpperBound);
	M6Iterator*		FindPattern(const std::string& inIndex, const std::string& inPattern);
	M6Iterator*		FindString(const std::string& inIndex, const std::string& inString);
	
	// Very low level...
	M6BasicIndexPtr	GetIndex(const std::string& inIndex) const;

	// retrieve links for a certain record
	void			InitLinkMap(const M6LinkMap& inLinkMap);
	bool			IsLinked(const std::string& inDb, const std::string& inId);

	// GetLinkedDocuments returns documents in this databank that are linked to
	// by inDb/inId or that link themselves to inDb/inId
	M6Iterator*		GetLinkedDocuments(const std::string& inDb, const std::string& inId);

	// Exist returns <documents exist,docnr for a unique match>
	std::tuple<bool,uint32>
					Exists(const std::string& inIndex, const std::string& inValue);

	// DocNrForID returns a doc number for an ID if the document exists
	// and zero if it doesn't.
	uint32			DocNrForID(const std::string& inID);
	
	// dictionary interface
	void			SuggestCorrection(const std::string& inWord,
						std::vector<std::pair<std::string,uint16>>& outCorrections);
	void			SuggestSearchTerms(const std::string& inWord,
						std::vector<std::string>& outSearchTerms);

	// for browsing
	bool			BrowseSectionsForIndex(const std::string& inIndex,
						const std::string& inFirst, const std::string& inLast,
						uint32 inRequestedBrowseSections,
						std::vector<std::pair<std::string,std::string>>& outBrowseSections);
	void			ListIndexEntries(const std::string& inIndex,
						std::vector<std::string>& outEntries);
	void			ListIndexEntries(const std::string& inIndex,
						const std::string& inFirst, const std::string& inLast,
						std::vector<std::string>& outEntries);

	M6DocStore&		GetDocStore();
	
	uint32			size() const;
	uint32			GetMaxDocNr() const;
	
  private:

	friend class M6DatabankImpl;

					// private constructor to create a new databank
					M6Databank(const std::string& inDatabankID, const boost::filesystem::path& inPath,
						const std::string& inVersion, const std::vector<std::pair<std::string,std::string>>& inIndexNames);

	M6DatabankImpl*	mImpl;
};
