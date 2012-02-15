#include "M6Lib.h"

#include <set>
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>

#include "M6Databank.h"
#include "M6Document.h"
#include "M6DocStore.h"
#include "M6Error.h"
#include "M6BitStream.h"
#include "M6Index.h"
#include "M6SortedRunArray.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

const uint32
	kM6WeightBitCount = 5,
	kM6MaxWeight = (1 << kM6WeightBitCount) - 1,
	kMaxIndexNr = 30;

class M6BatchIndexProcessor;

// --------------------------------------------------------------------

typedef boost::shared_ptr<M6BasicIndex> M6BasicIndexPtr;

class M6DatabankImpl
{
  public:
					M6DatabankImpl(M6Databank& inDatabank, const string& inPath, MOpenMode inMode);
	virtual			~M6DatabankImpl();

	void			StartBatchImport(M6Lexicon& inLexicon);
	void			CommitBatchImport();
		
	void			Store(M6Document* inDocument);
	M6Document*		Fetch(uint32 inDocNr);
	M6Document*		FindDocument(const string& inIndex, const string& inValue);
	M6Iterator*		Find(const string& inQuery, uint32 inReportLimit);
	
	M6DocStore&		GetDocStore()						{ return *mStore; }
	
	M6BasicIndexPtr	GetIndex(const string& inName);
	M6BasicIndexPtr	CreateIndex(const string& inName, M6IndexType inType, bool inUnique = false);
	M6BasicIndexPtr	GetAllTextIndex()					{ return mAllTextIndex; }
	
	fs::path		GetScratchDir() const				{ return mDbDirectory / "tmp"; }

	void			RecalculateDocumentWeights();

	void			Validate();

  protected:

	struct M6IndexDesc
	{
							M6IndexDesc(const string& inName, M6BasicIndexPtr inIndex)
								: mName(inName), mIndex(inIndex) {}

		string				mName;
		M6BasicIndexPtr		mIndex;
	};
	typedef vector<M6IndexDesc>	M6IndexDescList;
	
	M6Databank&				mDatabank;
	fs::path				mDbDirectory;
	MOpenMode				mMode;
	M6DocStore*				mStore;
	M6BatchIndexProcessor*	mBatch;
	M6IndexDescList			mIndices;
	M6BasicIndexPtr			mAllTextIndex;
	vector<float>			mDocWeights;
};

// --------------------------------------------------------------------

class M6FullTextIx
{
  public:

	struct BufferEntry
	{
		M6OBitStream	idl;
		uint8			ix;
		uint8			weight;	// for weighted keys
		uint32			term;
		uint32			doc;
		
		bool			operator<(const BufferEntry& inOther) const
							{ return term < inOther.term or
									(term == inOther.term and doc < inOther.doc) or
									(term == inOther.term and doc == inOther.doc and ix < inOther.ix); }
	};
	
	struct BufferEntryWriter
	{
						BufferEntryWriter(M6FullTextIx& inFullTextIndex)
							: mFirstDoc(0), mFullTextIndex(&inFullTextIndex) { }

		void			PrepareForWrite(const BufferEntry* values, uint32 count);
		void			WriteSortedRun(M6FileStream& inFile, const BufferEntry* values, uint32 count);
		
		uint32			mFirstDoc;
		M6FullTextIx*	mFullTextIndex;
	};
	
	struct BufferEntryReader
	{
						BufferEntryReader(M6FileStream& file, int64 offset);
		void			ReadSortedRunEntry(BufferEntry& value);

		M6IBitStream	bits;
		uint32			term;
		uint32			doc;
		uint32			mFirstDoc;
		uint32			idlIxMap;
	};
	
	// the number of buffer entries is one of the most important
	// variables affecting indexing speed and memory consumption.
	// The value chosen here seems to be a reasonable tradeoff.
	enum {
		kBufferEntryCount = 800000
//		kBufferEntryCount = 800
	};
	
	typedef M6SortedRunArray
	<
		BufferEntry,
		less<BufferEntry>,
		kBufferEntryCount,
		BufferEntryWriter,
		BufferEntryReader
	>	M6EntryBuffer;

	typedef M6EntryBuffer::iterator M6EntryIterator;

					M6FullTextIx(const fs::path& inScratch);
	virtual			~M6FullTextIx();
	
	void			SetUsesInDocLocation(uint32 inIndexNr)		{ mDocLocationIxMap |= (1 << inIndexNr); }
	bool			UsesInDocLocation(uint32 inIndexNr) const	{ return mDocLocationIxMap & (1 << inIndexNr); }
	uint32			GetDocLocationIxMap() const					{ return mDocLocationIxMap; }

	void			AddWord(uint8 inIndex, uint32 inWord);
	void			FlushDoc(uint32 inDocNr);

	M6EntryIterator*Finish()									{ return mEntries.Finish(); }
	int64			CountEntries() const						{ return mEntries.Size(); }
	
	fs::path		GetScratchDir() const						{ return mScratchDir; }

  private:
	
	typedef vector<uint32>		DocLoc;

	struct DocWord
	{
		uint32		word;
		uint32		index;
		uint32		freq;
		DocLoc		loc;
		
		bool		operator<(const DocWord& inOther) const
						{ return word < inOther.word or (word == inOther.word and index < inOther.index); }
	};

	typedef set<DocWord> DocWords;
	
	DocWords		mDocWords;
	uint32			mDocLocationIxMap;
	uint32			mDocWordLocation;

	BufferEntryWriter
					mBufferEntryWriter;
	fs::path		mScratchDir;
	M6EntryBuffer	mEntries;
};

ostream& operator<<(ostream& os, const M6FullTextIx::BufferEntry& e)
{
	os << e.term << '\t'
	   << e.doc << '\t'
	   << uint32(e.ix) << '\t'
	   << uint32(e.weight);
	return os;
}

M6FullTextIx::M6FullTextIx(const fs::path& inScratchUrl)
	: mDocLocationIxMap(0)
	, mDocWordLocation(1)
	, mBufferEntryWriter(*this)
	, mScratchDir(inScratchUrl)
	, mEntries((mScratchDir / "fulltext").string(), less<BufferEntry>(), mBufferEntryWriter)
{
}

M6FullTextIx::~M6FullTextIx()
{
}

void M6FullTextIx::AddWord(uint8 inIndex, uint32 inWord)
{
	++mDocWordLocation;	// always increment, no matter if we do not add the word
	
	if (inWord > 0)
	{
		if (inIndex > kMaxIndexNr)
			THROW(("Too many full text indices"));
		
		DocWord w = { inWord, inIndex, 1 };
		
		DocWords::iterator i = mDocWords.find(w);
		if (i != mDocWords.end())
			const_cast<DocWord&>(*i).freq += 1;
		else
			i = mDocWords.insert(w).first;
		
		if (UsesInDocLocation(inIndex))
		{
			DocWord& dw = const_cast<DocWord&>(*i);
			dw.loc.push_back(mDocWordLocation);
		}
	}
}

void M6FullTextIx::FlushDoc(uint32 inDoc)
{
	// normalize the frequencies.
	uint32 maxFreq = 1;
	
	for (DocWords::iterator w = mDocWords.begin(); w != mDocWords.end(); ++w)
		if (w->freq > maxFreq)
			maxFreq = w->freq;

	for (DocWords::iterator w = mDocWords.begin(); w != mDocWords.end(); ++w)
	{
		if (w->freq == 0)
			continue;
		
		BufferEntry e = {};
		
		e.term = w->word;
		e.doc = inDoc;
		e.ix = w->index;

		e.weight = (w->freq * kM6MaxWeight) / maxFreq;
		if (e.weight < 1)
			e.weight = 1;
		
		if (UsesInDocLocation(w->index))
		{
			DocLoc& loc = const_cast<DocLoc&>(w->loc);
			assert(e.idl.BitSize() == 0);
			WriteArray(e.idl, loc);
		}

		mEntries.PushBack(e);
	}
	
	mDocWords.clear();
	mDocWordLocation = 1;
}

void M6FullTextIx::BufferEntryWriter::PrepareForWrite(const BufferEntry* inValues, uint32 inCount)
{
	mFirstDoc = inValues[0].doc;
}

void M6FullTextIx::BufferEntryWriter::WriteSortedRun(M6FileStream& inFile, const BufferEntry* inValues, uint32 inCount)
{
	M6OBitStream bits(inFile);
	
	uint32 t = 0;
	uint32 d = mFirstDoc;	// the first doc in this run
	int32 ix = -1;
	
	uint32 idlIxMap = mFullTextIndex->GetDocLocationIxMap();
	
	WriteGamma(bits, mFirstDoc);
	WriteBinary(bits, 32, idlIxMap);

	for (uint32 i = 0; i < inCount; ++i)
	{
		assert(inValues[i].term > t or inValues[i].doc > d or inValues[i].ix > ix);

		if (inValues[i].term > t)
			d = mFirstDoc;

		WriteGamma(bits, inValues[i].term - t + 1);
		WriteGamma(bits, inValues[i].doc - d + 1);
		WriteGamma(bits, inValues[i].ix + 1);
		WriteBinary(bits, kM6WeightBitCount, inValues[i].weight);

		if (idlIxMap & (1 << inValues[i].ix))
			WriteBits(bits, inValues[i].idl);

		t = inValues[i].term;
		d = inValues[i].doc;
		ix = inValues[i].ix;
	}
	
	bits.Sync();
}

M6FullTextIx::BufferEntryReader::BufferEntryReader(M6FileStream& inFile, int64 inOffset)
	: bits(inFile, inOffset)
{
	ReadGamma(bits, mFirstDoc);
	doc = mFirstDoc;
	term = 0;
	ReadBinary(bits, 32, idlIxMap);
}

void M6FullTextIx::BufferEntryReader::ReadSortedRunEntry(BufferEntry& outValue)
{
	uint32 delta;
	
	ReadGamma(bits, delta);
	delta -= 1;

	if (delta)
	{
		term += delta;
		doc = mFirstDoc;
	}
	
	outValue.term = term;
	ReadGamma(bits, delta);
	delta -= 1;
	doc += delta;
	outValue.doc = doc;
	ReadGamma(bits, outValue.ix);
	outValue.ix -= 1;
	ReadBinary(bits, kM6WeightBitCount, outValue.weight);
	
	if (idlIxMap & (1 << outValue.ix))
		ReadBits(bits, outValue.idl);
}

// --------------------------------------------------------------------

class M6BasicIx
{
  public:
					M6BasicIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);
	virtual 		~M6BasicIx();
	
	void			AddWord(uint32 inWord);
	void			SetDbDocCount(uint32 inDbDocCount);
	void			AddDocTerm(uint32 inDoc, uint32 inTerm, uint8 inFrequency, M6OBitStream& inIDL);

	bool			Empty() const							{ return mLastDoc == 0; }
	uint8			GetIxNr() const							{ return mIndexNr; }
	string			GetName() const							{ return mName; }

  protected:
	
	virtual void	AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL);
	virtual void	FlushTerm(uint32 inTerm, uint32 inDocCount);

	string			mName;
	M6FullTextIx&	mFullTextIndex;
	M6Lexicon&		mLexicon;
	uint8			mIndexNr;
	
	// data for the second pass
	M6BasicIndexPtr	mIndex;

	uint32			mLastDoc;
	uint32			mLastTerm;
	M6OBitStream	mBits;
	uint32			mDocCount;
	uint32			mDbDocCount;
};

// --------------------------------------------------------------------
//	
//

M6BasicIx::M6BasicIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
	const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: mName(inName)
	, mFullTextIndex(inFullTextIndex)
	, mLexicon(inLexicon)
	, mIndexNr(inIndexNr)
	, mIndex(inIndex)
	, mLastDoc(0)
	, mDocCount(0)
	, mDbDocCount(0)
{
}

M6BasicIx::~M6BasicIx()
{
}

void M6BasicIx::AddWord(uint32 inWord)
{
	mFullTextIndex.AddWord(mIndexNr, inWord);
}

void M6BasicIx::SetDbDocCount(uint32 inDbDocCount)
{
	mDbDocCount = inDbDocCount;
	mLastDoc = 0;
	mLastTerm = 0;
}

void M6BasicIx::AddDocTerm(uint32 inDoc, uint32 inTerm, uint8 inFrequency, M6OBitStream& inIDL)
{
	if (inTerm != mLastTerm and not Empty())
		FlushTerm(mLastTerm, mDbDocCount);
	
	if (inDoc != 0)
	{
		mLastTerm = inTerm;
		AddDocTerm(inDoc, inFrequency, inIDL);
	}
}

void M6BasicIx::AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL)
{
	uint32 d;

	if (mBits.Empty())
	{
		mDocCount = 0;
		d = inDoc;
	}
	else
	{
		assert(inDoc > mLastDoc);
		d = inDoc - mLastDoc;
	}
	
	WriteGamma(mBits, d);
	
	mLastDoc = inDoc;
	++mDocCount;
}

void M6BasicIx::FlushTerm(uint32 inTerm, uint32 inDocCount)
{
	if (mDocCount > 0 and not mBits.Empty())
	{
		mBits.Sync();
		
		M6IBitStream bits(mBits);

		uint32 docNr = 0;
		vector<uint32> docs;
		
		for (uint32 d = 0; d < mDocCount; ++d)
		{
			uint32 delta;
			ReadGamma(bits, delta);
			docNr += delta;
			docs.push_back(docNr);
		}

		//static_cast<M6MultiBasicIndex*>(mIndex.get())->Insert(mLexicon.GetString(inTerm), docs, inDocCount);
		static_cast<M6MultiBasicIndex*>(mIndex.get())->Insert(mLexicon.GetString(inTerm), docs);
	}

	mBits.Clear();
	
	mDocCount = 0;
	mLastDoc = 0;
}

// --------------------------------------------------------------------
// M6ValueIx, can only store one value per document and so
// it should be unique

class M6ValueIx : public M6BasicIx
{
  public:
					M6ValueIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);
	
	virtual void	AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL);
	virtual void	FlushTerm(uint32 inTerm, uint32 inDocCount);
	
  private:
	vector<uint32>	mDocs;
};

M6ValueIx::M6ValueIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: M6BasicIx(inFullTextIndex, inLexicon, inName, inIndexNr, inIndex)
{
}
	
void M6ValueIx::AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL)
{
	mDocs.push_back(inDoc);
	mLastDoc = inDoc;
}

void M6ValueIx::FlushTerm(uint32 inTerm, uint32 inDocCount)
{
	if (mDocs.size() != 1)
	{
		string term = mLexicon.GetString(inTerm);

		cerr << endl
			 << "Term " << term << " is not unique for index "
			 << mName << ", it appears in document: ";

		for (vector<uint32>::iterator d = mDocs.begin(); d != mDocs.end(); ++d)
			cerr << *d << " ";

		cerr << endl;
	}
	
	if (mDocs.size() > 0)
		mIndex->Insert(mLexicon.GetString(inTerm), mDocs.back());
	
	mDocs.clear();
}

// --------------------------------------------------------------------
//	Text Index contains a full text index

class M6TextIx : public M6BasicIx
{
  public:
					M6TextIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);
	virtual			~M6TextIx();

  private:
	void			AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL);
	virtual void	FlushTerm(uint32 inTerm, uint32 inDocCount);

	M6FileStream*	mIDLFile;
	M6OBitStream*	mIDLBits;
	int64			mIDLOffset;
};

M6TextIx::M6TextIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: M6BasicIx(inFullTextIndex, inLexicon, inName, inIndexNr, inIndex)
	, mIDLFile(nullptr)
	, mIDLBits(nullptr)
	, mIDLOffset(0)
{
	mFullTextIndex.SetUsesInDocLocation(mIndexNr);
	mIDLFile = new M6FileStream((mFullTextIndex.GetScratchDir().parent_path() / (inName + ".idl")).string(), eReadWrite);
}

M6TextIx::~M6TextIx()
{
	delete mIDLBits;
	delete mIDLFile;
}

void M6TextIx::AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL)
{
	M6BasicIx::AddDocTerm(inDoc, inFrequency, inIDL);
	
	if (mIDLBits == nullptr)
	{
		mIDLOffset = mIDLFile->Seek(0, SEEK_END);
		mIDLBits = new M6OBitStream(*mIDLFile);
	}

	// read binary is much faster than read gamma...
	int64 idlBitSize = inIDL.BitSize();
	if (idlBitSize <= 63)
		WriteBinary(*mIDLBits, 6, idlBitSize);
	else
	{
		WriteBinary(*mIDLBits, 6, 0);
		WriteGamma(*mIDLBits, idlBitSize);
	}

	CopyBits(*mIDLBits, inIDL);
}

void M6TextIx::FlushTerm(uint32 inTerm, uint32 inDocCount)
{
	if (mDocCount > 0 and not mBits.Empty())
	{
		// flush the raw index bits
		mBits.Sync();
		
		if (mIDLBits != nullptr)
			mIDLBits->Sync();

		vector<uint32> docs;
		
		M6IBitStream bits(mBits);

		uint32 docNr = 0;
		
		for (uint32 d = 0; d < mDocCount; ++d)
		{
			uint32 delta;
			ReadGamma(bits, delta);
			docNr += delta;
			docs.push_back(docNr);
		}

//		static_cast<M6MultiIDLBasicIndex*>(mIndex.get())->Insert(mLexicon.GetString(inTerm), mIDLOffset, docs, inDocCount);
		static_cast<M6MultiIDLBasicIndex*>(mIndex.get())->Insert(mLexicon.GetString(inTerm), mIDLOffset, docs);
	}

	mBits.Clear();
	
	delete mIDLBits;
	mIDLBits = nullptr;
	
	mDocCount = 0;
	mLastDoc = 0;
}

// --------------------------------------------------------------------
//	Weighted word index, used for ranked searching

class M6WeightedWordIx : public M6BasicIx
{
  public:
					M6WeightedWordIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);

	virtual void	AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL);
	virtual void	FlushTerm(uint32 inTerm, uint32 inDocCount);
};

M6WeightedWordIx::M6WeightedWordIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: M6BasicIx(inFullTextIndex, inLexicon, inName, inIndexNr, inIndex)
{
}

void M6WeightedWordIx::AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL)
{
	uint32 d;

	if (mBits.Empty())
	{
		mDocCount = 0;
		d = inDoc;
	}
	else
	{
		assert(inDoc > mLastDoc);
		d = inDoc - mLastDoc;
	}
	
	WriteGamma(mBits, d);
	
	if (inFrequency < 1)
		inFrequency = 1;
	else if (inFrequency >= kM6MaxWeight)
		inFrequency = kM6MaxWeight;
	
	WriteBinary(mBits, kM6WeightBitCount, inFrequency);
	
	mLastDoc = inDoc;
	++mDocCount;
}

void M6WeightedWordIx::FlushTerm(uint32 inTerm, uint32 inDocCount)
{
	if (mDocCount > 0 and not mBits.Empty())
	{
		// flush the raw index bits
		mBits.Sync();
		
		vector<pair<uint32,uint8>> docs;
		
		M6IBitStream bits(mBits);

		uint32 docNr = 0;
		for (uint32 d = 0; d < mDocCount; ++d)
		{
			uint32 delta;
			ReadGamma(bits, delta);
			docNr += delta;
			assert(docNr <= inDocCount);
			uint8 weight;
			ReadBinary(bits, kM6WeightBitCount, weight);
			assert(weight > 0);
			docs.push_back(make_pair(docNr, weight));
		}

//		static_cast<M6WeightedBasicIndex*>(mIndex.get())->Insert(mLexicon.GetString(inTerm), docs, inDocCount);
		static_cast<M6WeightedBasicIndex*>(mIndex.get())->Insert(mLexicon.GetString(inTerm), docs);
	}

	mBits.Clear();
	
	mDocCount = 0;
	mLastDoc = 0;
}

// --------------------------------------------------------------------
//	Date Index, only dates

class M6DateIx : public M6BasicIx
{
  public:
					M6DateIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);
};

M6DateIx::M6DateIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: M6BasicIx(inFullTextIndex, inLexicon, inName, inIndexNr, inIndex)
{
}

// --------------------------------------------------------------------
//	Number Index, only numbers

class M6NumberIx : public M6BasicIx
{
  public:
					M6NumberIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);
//
//	virtual int		Compare(const char* inA, uint32 inLengthA, const char* inB, uint32 inLengthB) const;
};

M6NumberIx::M6NumberIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: M6BasicIx(inFullTextIndex, inLexicon, inName, inIndexNr, inIndex)
{
}

//int M6NumberIx::Compare(const char* inA, uint32 inLengthA,
//	const char* inB, uint32 inLengthB) const
//{
//	M6IndexTraits<kNumberIndex> comp;
//	return comp.Compare(inA, inLengthA, inB, inLengthB);
//}

// --------------------------------------------------------------------

class M6BatchIndexProcessor
{
  public:
				M6BatchIndexProcessor(M6DatabankImpl& inDatabank, M6Lexicon& inLexicon);
				~M6BatchIndexProcessor();

	void		IndexTokens(const string& inIndexName, M6DataType inDataType,
					const M6InputDocument::M6TokenList& inTokens);
	void		IndexValue(const string& inIndexName, M6DataType inDataType,
					const string& inValue, bool inUnique, uint32 inDocNr);
	void		FlushDoc(uint32 inDocNr);
	void		Finish(uint32 inDocCount);

  private:

	template<class T>
	M6BasicIx*	GetIndexBase(const string& inName, M6IndexType inType);

	M6FullTextIx		mFullTextIndex;
	M6DatabankImpl&		mDatabank;
	M6Lexicon&			mLexicon;
	vector<M6BasicIx*>	mIndices;
};

M6BatchIndexProcessor::M6BatchIndexProcessor(M6DatabankImpl& inDatabank, M6Lexicon& inLexicon)
	: mFullTextIndex(inDatabank.GetScratchDir())
	, mDatabank(inDatabank)
	, mLexicon(inLexicon)
{
}

M6BatchIndexProcessor::~M6BatchIndexProcessor()
{
	foreach (M6BasicIx* ix, mIndices)
		delete ix;
}

template<class T>
M6BasicIx* M6BatchIndexProcessor::GetIndexBase(const string& inName, M6IndexType inType)
{
	vector<M6BasicIx*>::iterator ix = find_if(mIndices.begin(), mIndices.end(),
		boost::bind(&M6BasicIx::GetName, _1) == inName);
	
	if (ix == mIndices.end())
	{
		M6BasicIndexPtr index = mDatabank.CreateIndex(inName, inType);

		index->SetAutoCommit(false);
		
		mIndices.push_back(new T(mFullTextIndex, mLexicon, inName,
			static_cast<uint8>(mIndices.size() + 1), index));
		ix = mIndices.end() - 1;
	}
	
	return *ix;
}

void M6BatchIndexProcessor::IndexTokens(const string& inIndexName,
	M6DataType inDataType, const M6InputDocument::M6TokenList& inTokens)
{
	if (not inTokens.empty())
	{
		if (inDataType == eM6StringData)
		{
			foreach (uint32 t, inTokens)
				mFullTextIndex.AddWord(0, t);
		}
		else
		{
			M6BasicIx* index = GetIndexBase<M6TextIx>(inIndexName, eM6FullTextIndexType);
			foreach (uint32 t, inTokens)
				index->AddWord(t);
		}
	}
}

void M6BatchIndexProcessor::IndexValue(const string& inIndexName,
	M6DataType inDataType, const string& inValue, bool inUnique, uint32 inDocNr)
{
	if (inUnique)
	{
		M6BasicIndexPtr index;
	
		switch (inDataType)
		{
			case eM6StringData:	index = mDatabank.CreateIndex(inIndexName, eM6StringIndexType, true); break;
			case eM6NumberData:	index = mDatabank.CreateIndex(inIndexName, eM6NumberIndexType, true); break;
			case eM6DateData:	index = mDatabank.CreateIndex(inIndexName, eM6DateIndexType, true); break;
			default:			THROW(("Runtime error, unexpected index type"));
		}
		
		index->SetAutoCommit(false);
	
		index->Insert(inValue, inDocNr);
	}
	else
	{
		// too bad, we still have to go through the old route
		
		M6BasicIx* index;

		switch (inDataType)
		{
			case eM6StringData:	index = GetIndexBase<M6TextIx>(inIndexName, eM6StringIndexType); break;
			case eM6NumberData:	index = GetIndexBase<M6NumberIx>(inIndexName, eM6NumberIndexType); break;
			case eM6DateData:	index = GetIndexBase<M6DateIx>(inIndexName, eM6DateIndexType); break;
			default:			THROW(("Runtime error, unexpected index type"));
		}

		mLexicon.LockUnique();
		uint32 t = mLexicon.Store(inValue);
		mLexicon.UnlockUnique();
		
		index->AddWord(t);
	}
}

void M6BatchIndexProcessor::FlushDoc(uint32 inDocNr)
{
	mFullTextIndex.FlushDoc(inDocNr);
}

void M6BatchIndexProcessor::Finish(uint32 inDocCount)
{
	// add the required 'alltext' index
	mIndices.push_back(new M6WeightedWordIx(mFullTextIndex, mLexicon, "all-text",
		static_cast<uint8>(mIndices.size() + 1), mDatabank.GetAllTextIndex()));
	
	// tell indices about the doc count
	for_each(mIndices.begin(), mIndices.end(), [&inDocCount](M6BasicIx* ix) { ix->SetDbDocCount(inDocCount); });
	
	// get the iterator for all index entries
	auto_ptr<M6FullTextIx::M6EntryIterator> iter(mFullTextIndex.Finish());

	int64 entryCount = mFullTextIndex.CountEntries();
	int64 entriesRead = 0;
	int64 vStep = entryCount / 10;
	
	if (vStep == 0)
		vStep = 1;

	//// update progress information
	//if (inProgress != nullptr)
	//	inProgress->SetCreateIndexProgress(0, entryCount);
	
	// the next loop is very *hot*, make sure it is optimized as much as possible.
	// 
	M6FullTextIx::BufferEntry ie = {};
	if (not iter->Next(ie))
		THROW(("Nothing was indexed..."));

	uint32 lastTerm = ie.term;
	uint32 lastDoc = ie.doc;
	uint32 termFrequency = ie.weight;

	do
	{
		++entriesRead;
	
//		if (inProgress != nullptr and (entriesRead % 1000000) == 0)
//			inProgress->SetCreateIndexProgress(entriesRead, entryCount);

		if (lastDoc != ie.doc or lastTerm != ie.term)
		{
			mIndices.back()->AddDocTerm(lastDoc, lastTerm, termFrequency, ie.idl);

			lastDoc = ie.doc;
			lastTerm = ie.term;
			termFrequency = 0;
		}
		
		if (ie.ix > 0)
			mIndices[ie.ix - 1]->AddDocTerm(ie.doc, ie.term, ie.weight, ie.idl);
		termFrequency += ie.weight;
		
		if (termFrequency > numeric_limits<uint8>::max())
			termFrequency = numeric_limits<uint8>::max();
	}
	while (iter->Next(ie));
	
	// flush
	for_each(mIndices.begin(), mIndices.end(), [&ie](M6BasicIx* ix) { ix->AddDocTerm(0, 0, 0, ie.idl); });
}

// --------------------------------------------------------------------

M6DatabankImpl::M6DatabankImpl(M6Databank& inDatabank, const string& inPath, MOpenMode inMode)
	: mDatabank(inDatabank)
	, mDbDirectory(inPath)
	, mMode(inMode)
	, mStore(nullptr)
	, mBatch(nullptr)
{
	if (not fs::exists(mDbDirectory) and inMode == eReadWrite)
	{
		fs::create_directory(mDbDirectory);
		fs::create_directory(mDbDirectory / "tmp");
		
		mStore = new M6DocStore((mDbDirectory / "data").string(), eReadWrite);
		mAllTextIndex.reset(new M6SimpleWeightedIndex((mDbDirectory / "all-text.index").string(), eReadWrite));
	}
	else if (not fs::is_directory(mDbDirectory))
		THROW(("databank path is invalid (%s)", inPath.c_str()));
	else
	{
		mStore = new M6DocStore((mDbDirectory / "data").string(), inMode);
		mAllTextIndex.reset(new M6SimpleWeightedIndex((mDbDirectory / "all-text.index").string(), inMode));
		
		if (fs::exists(mDbDirectory / "all-text.weights"))
		{
			try
			{
				uint32 maxDocNr = mStore->NextDocumentNumber();
				mDocWeights.assign(maxDocNr, 0);
				
				M6FileStream file((mDbDirectory / "all-text.weights").string(), eReadOnly);
				if (file.Size() == sizeof(float) * maxDocNr)
					file.Read(&mDocWeights[0], sizeof(float) * maxDocNr);
				else
					mDocWeights.clear();
			}
			catch (...)
			{
				mDocWeights.clear();
			}
		}
		
		if (mDocWeights.empty())
			RecalculateDocumentWeights();
	}
}

M6DatabankImpl::~M6DatabankImpl()
{
	mStore->Commit();
	delete mStore;
}

M6BasicIndexPtr M6DatabankImpl::GetIndex(const string& inName)
{
	M6BasicIndexPtr result;
	
	foreach (M6IndexDesc& desc, mIndices)
	{
		if (desc.mName == inName)
		{
			result = desc.mIndex;
			break;
		}
	}
	
	return result;
}

M6BasicIndexPtr M6DatabankImpl::CreateIndex(const string& inName, M6IndexType inType, bool inUnique)
{
	M6BasicIndexPtr result = GetIndex(inName);
	if (result == nullptr)
	{
		string path = (mDbDirectory / (inName + ".index")).string();
		
		if (inUnique)
		{
			switch (inType)
			{
				case eM6StringIndexType:
				case eM6DateIndexType:		result.reset(new M6SimpleIndex(path, mMode)); break;
				case eM6NumberIndexType:	result.reset(new M6NumberIndex(path, mMode)); break;
				default:					THROW(("unsupported"));
			}
		}
		else
		{
			switch (inType)
			{
				case eM6DateIndexType:		result.reset(new M6SimpleMultiIndex(path, mMode)); break;
				case eM6NumberIndexType:	result.reset(new M6NumberMultiIndex(path, mMode)); break;
				case eM6StringIndexType:
				case eM6FullTextIndexType:	result.reset(new M6SimpleIDLMultiIndex(path, mMode)); break;
				default:					THROW(("unsupported"));
			}
		}

		mIndices.push_back(M6IndexDesc(inName, result));
	}
	return result;
}
	
void M6DatabankImpl::Store(M6Document* inDocument)
{
	M6InputDocument* doc = dynamic_cast<M6InputDocument*>(inDocument);
	if (doc == nullptr)
		THROW(("Invalid document"));

	uint32 docNr = doc->Store();
	
	if (mBatch != nullptr)
	{
		foreach (const M6InputDocument::M6IndexTokens& d, doc->GetIndexTokens())
			mBatch->IndexTokens(d.mIndexName, d.mDataType, d.mTokens);

		foreach (const M6InputDocument::M6IndexValue& v, doc->GetIndexValues())
			mBatch->IndexValue(v.mIndexName, v.mDataType, v.mIndexValue, v.mUnique, docNr);

		mBatch->FlushDoc(docNr);
	}
	
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

M6Document* M6DatabankImpl::FindDocument(const string& inIndex, const string& inValue)
{
	M6BasicIndexPtr index = CreateIndex(inIndex, eM6StringIndexType);
	if (not index)
		THROW(("Index %s not found", inIndex.c_str()));
	
	uint32 v;
	if (not index->Find(ba::to_lower_copy(inValue), v))
		THROW(("Value %s not found in index %s", inValue.c_str(), inIndex.c_str()));
	
	return Fetch(v);
}

class M6Accumulator
{
  public:
				M6Accumulator(uint32 inDocCount)
					: mItems(reinterpret_cast<M6Item*>(calloc(sizeof(M6Item), inDocCount)))
					, mFirst(nullptr), mDocCount(inDocCount), mHitCount(0) {}
				
				~M6Accumulator()
				{
					free(mItems);
				}
				
	float		Add(uint32 inDocNr, float inDelta)
				{
					if (mItems[inDocNr].mCount++ == 0)
					{
						mItems[inDocNr].mNext = mFirst;
						mFirst = &mItems[inDocNr];
						++mHitCount;
					}
					
					return mItems[inDocNr].mValue += inDelta;
				}

	float		operator[](uint32 inIndex) const	{ return mItems[inIndex].mValue; }
	
	void		Collect(vector<uint32>& outDocs, uint32 inTermCount)
				{
					outDocs.reserve(mHitCount);
					for (M6Item* item = mFirst; item != nullptr; item = item->mNext)
					{
						if (item->mCount >= inTermCount)
							outDocs.push_back(static_cast<uint32>(item - mItems));
					}
				}

  private:
	
	struct M6Item
	{
		float	mValue;
		uint32	mCount;
		M6Item*	mNext;
	};
	
	M6Item*		mItems;
	M6Item*		mFirst;
	uint32		mDocCount, mHitCount;
};

M6Iterator* M6DatabankImpl::Find(const string& inQuery, uint32 inReportLimit)
{
	if (mDocWeights.empty())
		RecalculateDocumentWeights();

	M6Iterator* result = nullptr;

	uint32 docCount = mStore->size();
	uint32 maxDocNr = mStore->NextDocumentNumber();
	float maxD = static_cast<float>(maxDocNr);

	float queryWeight = 0, Smax = 0;
	M6Accumulator A(maxDocNr);
	
	// for each term
	{
		M6WeightedBasicIndex::M6WeightedIterator iter;
		if (static_cast<M6WeightedBasicIndex*>(mAllTextIndex.get())->Find(inQuery, iter))
		{
			uint8 termWeight = 1;	// ?
		
			const float c_add = 0.007f;
			const float c_ins = 0.12f;
	
			float s_add = c_add * Smax;
			float s_ins = c_ins * Smax;
			
			float idf = log(1.f + maxD / iter.Size());
			float wq = idf * termWeight;
			queryWeight += wq * wq;
			
			uint8 f_add = static_cast<uint8>(s_add / (idf * wq * wq));
			uint8 f_ins = static_cast<uint8>(s_ins / (idf * wq * wq));
			
			uint32 docNr;
			uint8 weight;
			while (iter.Next(docNr, weight) and weight >= f_add)
			{
				if (weight >= f_ins or A[docNr] != 0)
				{
					float wd = weight;
					float sd = idf * wd * wq;
					
					float S = A.Add(docNr, sd);
					if (Smax < S)
						Smax = S;
				}
			}
		}
	}
		
	queryWeight = sqrt(queryWeight);
	
	vector<uint32> docs;
	A.Collect(docs, 1);
	
	auto compare = [](const pair<uint32,float>& a, const pair<uint32,float>& b) -> bool
						{ return a.second > b.second; };
	vector<pair<uint32,float>> best;
	
	foreach (uint32 doc, docs)
	{
		float docWeight = mDocWeights[doc];
		float rank = A[doc] / (docWeight * queryWeight);
		
		if (best.size() < inReportLimit)
		{
			best.push_back(make_pair(doc, rank));
			push_heap(best.begin(), best.end(), compare);
		}
		else if (best.front().second < rank)
		{
			pop_heap(best.begin(), best.end(), compare);
			best.back() = make_pair(doc, rank);
			push_heap(best.begin(), best.end(), compare);
		}
	}
	
	sort_heap(best.begin(), best.end(), compare);
	
	foreach (auto b, best)
	{
		auto_ptr<M6Document> doc(Fetch(b.first));

		cout << doc->GetAttribute("id") << '\t'
			<< doc->GetAttribute("title") << '\t'
			<< b.second << endl;
	}
	
	return result;
}

void M6DatabankImpl::StartBatchImport(M6Lexicon& inLexicon)
{
	mBatch = new M6BatchIndexProcessor(*this, inLexicon);
}

void M6DatabankImpl::CommitBatchImport()
{
	mBatch->Finish(mStore->size());
	delete mBatch;
	mBatch = nullptr;

	foreach (M6IndexDesc& desc, mIndices)
		desc.mIndex->SetAutoCommit(true);
	
	// And clean up
	fs::remove_all(mDbDirectory / "tmp");

	RecalculateDocumentWeights();
}

void M6DatabankImpl::RecalculateDocumentWeights()
{
	uint32 docCount = mStore->size();
	uint32 maxDocNr = mStore->NextDocumentNumber();
	
	// recalculate document weights
	
	mDocWeights.assign(maxDocNr, 0);
	M6WeightedBasicIndex* ix = dynamic_cast<M6WeightedBasicIndex*>(mAllTextIndex.get());
	if (ix == nullptr)
		THROW(("Invalid index"));
	ix->CalculateDocumentWeights(docCount, mDocWeights);

	M6FileStream weightFile((mDbDirectory / "all-text.weights").string(), eReadWrite);
	weightFile.Write(&mDocWeights[0], sizeof(float) * mDocWeights.size());
}

void M6DatabankImpl::Validate()
{
	mStore->Validate();
	mAllTextIndex->Validate();
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

void M6Databank::StartBatchImport(M6Lexicon& inLexicon)
{
	mImpl->StartBatchImport(inLexicon);
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

M6Document* M6Databank::FindDocument(const string& inIndex, const string& inValue)
{
	return mImpl->FindDocument(inIndex, inValue);
}

M6Iterator* M6Databank::Find(const string& inQuery, uint32 inReportLimit)
{
	return mImpl->Find(inQuery, inReportLimit);
}

uint32 M6Databank::size() const
{
	return mImpl->GetDocStore().size();
}

void M6Databank::RecalculateDocumentWeights()
{
	mImpl->RecalculateDocumentWeights();
}

void M6Databank::Validate()
{
	mImpl->Validate();
}
