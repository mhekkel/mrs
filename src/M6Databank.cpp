#include "M6Lib.h"

#include "M6Databank.h"
#include "M6Document.h"

using namespace std;

M6Databank::M6Databank(const string& inPath, MOpenMode inMode)
	: mImpl(nullptr)
{
}

M6Databank::~M6Databank()
{
	delete mImpl;
}

M6Databank* M6Databank::CreateNew(const std::string& inPath)
{
	return new M6Databank(inPath, eReadWrite);
}

void M6Databank::Commit()
{
}

void M6Databank::Store(M6Document* inDocument)
{
	delete inDocument;
}
