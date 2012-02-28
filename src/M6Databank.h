#pragma once

#include "M6File.h"

class M6Document;
class M6DatabankImpl;
class M6DocStore;
class M6Lexicon;
class M6Iterator;

class M6Databank
{
  public:
					M6Databank(const boost::filesystem::path& inPath,
						MOpenMode inMode);
	virtual			~M6Databank();

	static M6Databank*
					CreateNew(const boost::filesystem::path& inPath);

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
	
	M6DocStore&		GetDocStore();
	
	uint32			size() const;

  private:

	M6DatabankImpl*	mImpl;
};
