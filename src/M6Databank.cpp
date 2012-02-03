#include "M6Lib.h"

#include <boost/filesystem.hpp>

#include "M6Databank.h"
#include "M6Document.h"
#include "M6DocStore.h"
#include "M6Error.h"

using namespace std;
namespace fs = boost::filesystem;

// --------------------------------------------------------------------

class M6BatchIndexProcessor
{
  public:
				M6BatchIndexProcessor();
				~M6BatchIndexProcessor();

	void		IndexTokens(const string& inIndexName, M6IndexKind inIndexKind,
					const M6InputDocument::M6TokenList& inTokens);
	void		FlushDoc();


  private:
	M6FullTextIndex&	mFullTextIndex;
	uint32				mEntries;
};

void M6BatchIndexProcessor::IndexTokens(const string& inIndexName,
	M6IndexKind inIndexKind, const M6InputDocument::M6TokenList& inTokens)
{
	if (not inTokens.empty())
	{
		M6IndexBase* index = nullptr;
	
		switch (inIndexKind)
		{
			case eM6TextIndex:		index = GetIndexBase<CTextIndex>(inIndexName); break;
			case eM6NumberIndex:	index = GetIndexBase<CNumberIndex>(inIndexName); break;
			case eM6ValueIndex:		index = GetIndexBase<CValueIndex>(inIndexName); break;
			case eM6DateIndex:		index = GetIndexBase<CDateIndex>(inIndexName); break;
			default:				THROW(("Runtime error, unsupport index kind"));
		}
	
		if (index == nullptr)
			THROW(("Runtime error"));
	
		foreach (M6InputDocument::M6TokenData& t, inTokens)
		{
			if (t.mGlobalToken != 0)
				index->AddWord(t.mGlobalToken);
			else
				mFullTextIndex->Stop();
		}
	}
}

void M6BatchIndexProcessor::FlushDoc()
{
	mFullTextIndex->FlushDoc(mEntries);
	mEntries += 1;
}

// --------------------------------------------------------------------

class M6DatabankImpl
{
  public:
				M6DatabankImpl(M6Databank& inDatabank, const string& inPath, MOpenMode inMode);
	virtual		~M6DatabankImpl();

	void		StartBatchImport();
	void		CommitBatchImport();
		
	void		Store(M6Document* inDocument);
	M6Document* Fetch(uint32 inDocNr);

	M6DocStore&	GetDocStore()						{ return *mStore; }

  protected:
	M6Databank&				mDatabank;
	M6DocStore*				mStore;
	M6BatchIndexProcessor*	mBatch;
};

M6DatabankImpl::M6DatabankImpl(M6Databank& inDatabank, const string& inPath, MOpenMode inMode)
	: mDatabank(inDatabank)
	, mStore(nullptr)
{
	fs::path path(inPath);
	
	if (not fs::exists(path) and inMode == eReadWrite)
	{
		fs::create_directory(path);
		
		mStore = new M6DocStore((path / "data").string(), eReadWrite);
	}
	else if (not fs::is_directory(path))
		THROW(("databank path is invalid (%s)", inPath.c_str()));
	else
		mStore = new M6DocStore((path / "data").string(), inMode);
}

M6DatabankImpl::~M6DatabankImpl()
{
	mStore->Commit();
	delete mStore;
}
	
void M6DatabankImpl::Store(M6Document* inDocument)
{
	M6InputDocument* doc = dynamic_cast<M6InputDocument*>(inDocument);
	if (doc == nullptr)
		THROW(("Invalid document"));

	doc->Store();
	
	foreach (M6Document::M6IndexTokens& d, doc->GetIndexTokens())
		mBatch->IndexTokens(d->index_name, d->index_kind, d.mTokens);
	mBatch->FlushDoc();
	
	delete inDocument;
}

M6Document* M6DatabankImpl::Fetch(uint32 inDocNr)
{
	M6Document* result = nullptr;
	
	uint32 docPage, docSize;
	if (mStore->FetchDocument(inDocNr, docPage, docSize))
		result = new M6OutputDocument(mDatabank, inDocNr, docPage, docSize);

	return result;
}

// --------------------------------------------------------------------

M6Databank::M6Databank(const string& inPath, MOpenMode inMode)
	: mImpl(new M6DatabankImpl(*this, inPath, inMode))
{
}

M6Databank::~M6Databank()
{
	delete mImpl;
}

M6Databank* M6Databank::CreateNew(const std::string& inPath)
{
	if (fs::exists(inPath))
		fs::remove_all(inPath);

	return new M6Databank(inPath, eReadWrite);
}

void M6Databank::StartBatchImport()
{
	mImpl->StartBatchImport();
}

void M6Databank::CommitBatchImport()
{
	mImpl->CommitBatchImport();
}

void M6Databank::Store(M6Document* inDocument)
{
	mImpl->Store(inDocument);
}

M6DocStore& M6Databank::GetDocStore()
{
	return mImpl->GetDocStore();
}

M6Document* M6Databank::Fetch(uint32 inDocNr)
{
	return mImpl->Fetch(inDocNr);
}

uint32 M6Databank::size() const
{
	return mImpl->GetDocStore().size();
}
