#pragma once

#include "M6File.h"

class M6Document;
class M6DatabankImpl;
class M6DocStore;
class M6Lexicon;

class M6Databank
{
  public:
					M6Databank(const std::string& inPath,
						MOpenMode inMode);
	virtual			~M6Databank();

	static M6Databank*
					CreateNew(const std::string& inPath);

	void			StartBatchImport(M6Lexicon& inLexicon);
	void			CommitBatchImport();
	
	void			Store(M6Document* inDocument);
	M6Document*		Fetch(uint32 inDocNr);
	
	M6DocStore&		GetDocStore();
	
	uint32			size() const;

  private:
	M6DatabankImpl*	mImpl;
};
