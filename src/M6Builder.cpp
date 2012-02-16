#include "M6Lib.h"

#include <iostream>
#include <memory>
#include <list>

#define PCRE_STATIC
#include <pcre.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/tr1/tuple.hpp>
//#include <boost/timer/timer.hpp>
#include <boost/lexical_cast.hpp>

#include "M6DocStore.h"
#include "M6Error.h"
#include "M6Databank.h"
#include "M6Document.h"
#include "M6Builder.h"
#include "M6Config.h"

using namespace std;
using namespace std::tr1;
namespace zx = zeep::xml;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

// --------------------------------------------------------------------

M6DataType MapDataType(const string& inKind, M6DataType inDefault)
{
	M6DataType result = inDefault;
		 if (inKind == "text")		result = eM6TextData;
	else if (inKind == "string")	result = eM6StringData;
	else if (inKind == "numeric")	result = eM6NumberData;
	else if (inKind == "date")		result = eM6DateData;
	return result;
}

// --------------------------------------------------------------------

struct M6Expr;
typedef shared_ptr<M6Expr>	M6ExprPtr;
typedef list<M6ExprPtr>		M6ExprList;

// --------------------------------------------------------------------

class M6Processor
{
  public:
					M6Processor(M6Databank& inDatabank, M6Lexicon& inLexicon,
						zx::element* inTemplate);
	virtual			~M6Processor();
	
	void			ProcessFile(fs::path inFile);
	void			ProcessDocument(const string& inDocument);
	
  private:

	M6ExprPtr		ParseScript(zx::element* inScript);

	M6Databank&		mDatabank;
	M6Lexicon&		mLexicon;
	zx::element*	mConfig;
	M6ExprPtr		mScript;
	M6Document*		mDocument;
};

// --------------------------------------------------------------------
// The xml based script interpreter

struct M6Argument
{
					M6Argument(const char* inText, int inOVector[30])
						: mIteration(1), mText(inText + inOVector[0])
						, mLength(inOVector[1] - inOVector[0])
					{
						for (int i = 0; i < 10; ++i)
						{
							mMatchStarts[i] = inText + inOVector[2 * i];
							mMatchLengths[i] = inOVector[2 * i + 1] - inOVector[2 * i];
						}
					}

					M6Argument(const char* inText, uint32 inLength)
						: mIteration(1), mText(inText)
						, mLength(inLength)
					{
						for (int i = 0; i < 10; ++i)
						{
							mMatchStarts[i] = inText;
							mMatchLengths[i] = inLength;
						}
					}


	uint32			mIteration;
	const char*		mText;
	uint32			mLength;
	const char*		mMatchStarts[10];
	uint32			mMatchLengths[10];
	string			mScratch;
};

struct M6Expr
{
					M6Expr() {}
	virtual			~M6Expr() {}

	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const = 0;
};

struct M6ListExpr : public M6Expr
{
	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const;
	M6ExprList		mList;
};

bool M6ListExpr::Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
{
	bool result = true;
	foreach (M6ExprPtr proc, mList)
	{
		if (not proc->Evaluate(inDocument, arg))
		{
			result = false;
			break;
		}
	}
	return result;
}

struct M6ForeachExpr : public M6Expr
{
					M6ForeachExpr(const string& inPattern);
					~M6ForeachExpr();

	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const;
	
	pcre*			mRE;
	pcre_extra*		mInfo;
	M6ExprPtr		mLoop;
};

M6ForeachExpr::M6ForeachExpr(const string& inPattern)
	: mRE(nullptr), mInfo(nullptr)
{
	const char* error;
	int erroffset;
	
	mRE = pcre_compile(inPattern.c_str(), PCRE_MULTILINE | PCRE_UTF8 | PCRE_NEWLINE_LF,
		&error, &erroffset, nullptr);
	if (mRE == nullptr)
	{
		THROW(("PCRE compilation failed at offset %d: %s\npattern: >> %s <<\n",
			erroffset, error, inPattern.c_str()));
	}
	else
	{
		mInfo = pcre_study(mRE, 0, &error);
		if (error != nullptr)
			THROW(("Error studying compiled regular expression: %s", error));
	}
}

M6ForeachExpr::~M6ForeachExpr()
{
	if (mInfo != nullptr)
		pcre_free(mInfo);
	
	if (mRE != nullptr)
		pcre_free(mRE);
}

bool M6ForeachExpr::Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
{
	bool result = true;
	
	int options = 0, offset = 0, iteration = 1;
	while (offset < arg.mLength)
	{
		int ovector[30];

		int rc = pcre_exec(mRE, mInfo, arg.mText, arg.mLength, offset, options, ovector, 30);
		
		if (rc == PCRE_ERROR_NOMATCH)
		{
			if (options == 0)
				break;
			offset += 1;
			continue;
		}

		if (rc < 0)
			THROW(("Matching error %d\n", rc));
		
		M6Argument a(arg.mText, ovector);
		a.mIteration = iteration;
		if (not mLoop->Evaluate(inDocument, a))
		{
			result = false;
			break;
		}
		
		offset = ovector[1];
		if (offset == arg.mLength)
			options = PCRE_NOTEMPTY | PCRE_ANCHORED;

		++iteration;
	}

	return result;
}

struct M6SplitExpr : public M6Expr
{
					M6SplitExpr(const string& inSeparator);
					~M6SplitExpr();
	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const;

	pcre*			mRE;
	M6ExprPtr		mExpr;
};

M6SplitExpr::M6SplitExpr(const string& inSeparator)
{
	const char* error;
	int erroffset;
	
	mRE = pcre_compile(inSeparator.c_str(), PCRE_MULTILINE | PCRE_UTF8 | PCRE_NEWLINE_LF,
		&error, &erroffset, nullptr);
	if (mRE == nullptr)
	{
		THROW(("PCRE compilation failed at offset %d: %s\npattern: >> %s <<\n",
			erroffset, error, inSeparator.c_str()));
	}
}

M6SplitExpr::~M6SplitExpr()
{
	if (mRE != nullptr)
		pcre_free(mRE);
}

bool M6SplitExpr::Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
{
	bool result = true;
	
	int options = 0, offset = 0, iteration = 1;
	while (offset < arg.mLength)
	{
		int ovector[30];
		int rc = pcre_exec(mRE, nullptr, arg.mText, arg.mLength, offset, options, ovector, 30);
		
		if (rc == PCRE_ERROR_NOMATCH)
		{
			M6Argument a(arg.mText + offset, arg.mLength - offset);
			a.mIteration = iteration;
			result = mExpr->Evaluate(inDocument, a);
			break;
		}

		if (rc < 0)
			THROW(("Matching error %d\n", rc));
		
		M6Argument a(arg.mText + offset, ovector[0] - offset);
		a.mIteration = iteration;
		if (not mExpr->Evaluate(inDocument, a))
		{
			result = false;
			break;
		}
		
		offset = ovector[1];
		if (offset == arg.mLength)
			options = PCRE_NOTEMPTY | PCRE_ANCHORED;

		++iteration;
	}

	return result;
}

struct M6CaptureExpr : public M6ListExpr
{
					M6CaptureExpr(uint32 inNr, M6ExprPtr inExpr) : mNr(inNr), mExpr(inExpr) {}
	
	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
					{
						M6Argument a(arg.mMatchStarts[mNr], arg.mMatchLengths[mNr]);
						return mExpr->Evaluate(inDocument, a);
					}
	
	M6ExprPtr		mExpr;
	uint32			mNr;
};

struct M6TolowerExpr : public M6Expr
{
	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
					{
						// copy text since we're about to mutate it
						arg.mScratch.assign(arg.mText, arg.mLength);
						ba::to_lower(arg.mScratch);
						arg.mText = arg.mScratch.c_str();
						arg.mLength = arg.mScratch.length();
						return true;
					}
};

struct M6SubStrExpr : public M6Expr
{
					M6SubStrExpr(int32 inStart, int32 inLength)
						: mStart(inStart), mLength(inLength) {}
	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
					{
						if (mStart >= arg.mLength)
							arg.mLength = 0;
						else
						{
							arg.mText += mStart;
							arg.mLength -= mStart;
							if (arg.mLength > mLength)
								arg.mLength = mLength;
						}
						
						return true;
					}
	int32			mStart, mLength;
};

struct M6ReplaceExpr : public M6Expr
{
					M6ReplaceExpr(const string& inWhat, const string& inWith);
					~M6ReplaceExpr();

	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const;

	pcre*			mRE;
	string			mWith;
};

M6ReplaceExpr::M6ReplaceExpr(const string& inWhat, const string& inWith)
	: mWith(inWith)
{
	const char* error;
	int erroffset;
	
	mRE = pcre_compile(inWhat.c_str(), PCRE_MULTILINE | PCRE_UTF8 | PCRE_NEWLINE_LF,
		&error, &erroffset, nullptr);
	if (mRE == nullptr)
	{
		THROW(("PCRE compilation failed at offset %d: %s\npattern: >> %s <<\n",
			erroffset, error, inWhat.c_str()));
	}
}

M6ReplaceExpr::~M6ReplaceExpr()
{
	if (mRE != nullptr)
		pcre_free(mRE);
}

bool M6ReplaceExpr::Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
{
	bool result = true;
	
	int options = 0, offset = 0;
	
	string s;
	
	while (offset < arg.mLength)
	{
		int ovector[30];

		int rc = pcre_exec(mRE, nullptr, arg.mText, arg.mLength, offset, options, ovector, 30);
		
		if (rc == PCRE_ERROR_NOMATCH)
		{
			if (options == 0)
			{
				s.append(arg.mText + offset, arg.mLength - offset);
				break;
			}
			offset += 1;
			continue;
		}

		if (rc < 0)
			THROW(("Matching error %d\n", rc));
		
		s.append(arg.mText + offset, ovector[0] - offset);
		s.append(mWith);
		
		offset = ovector[1];
		if (offset == arg.mLength)
			options = PCRE_NOTEMPTY | PCRE_ANCHORED;
	}

	arg.mScratch = s;
	arg.mText = arg.mScratch.c_str();
	arg.mLength = arg.mScratch.length();

	return true;
}

struct M6SwitchExpr : public M6Expr
{
					M6SwitchExpr(uint32 inTest) : mTest(inTest) {}

	void			AddCase(const string& inValue, M6ExprPtr inExpr)
					{
						mCases.push_back(M6Case(inValue, inExpr));
					}
					
	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const;

	struct M6Case
	{
					M6Case(const string& inValue, M6ExprPtr inExpr)
						: mValue(inValue), mExpr(inExpr) {}
					M6Case(const M6Case& c) : mValue(c.mValue), mExpr(c.mExpr) {}
		
		string		mValue;
		M6ExprPtr	mExpr;
	};
	
	uint32			mTest;
	list<M6Case>	mCases;
	M6ExprPtr		mDefault;
};

bool M6SwitchExpr::Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
{
	bool result = true;
	
	string test(arg.mMatchStarts[mTest], arg.mMatchLengths[mTest]);
	
	bool handled = false;
	foreach (const M6Case& c, mCases)
	{
		if (test == c.mValue)
		{
			handled = true;
			result = c.mExpr->Evaluate(inDocument, arg);
			break;
		}
	}
	
	if (not handled and mDefault)
		result = mDefault->Evaluate(inDocument, arg);
	
	return result;
}

struct M6IfExpr : public M6Expr
{
					M6IfExpr(uint32 inIteration) : mIteration(inIteration) {}
	
	bool			Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
					{
						bool result = true;
						if (mIteration == arg.mIteration)
							result = mExpr->Evaluate(inDocument, arg);
						return result;
					}
	M6ExprPtr		mExpr;
	uint32			mIteration;
};

struct M6IndexExpr : public M6Expr
{
					M6IndexExpr(const string& inName, M6DataType inType, bool inUnique)
						: mName(inName), mType(inType) {}

	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
					{
						inDocument->Index(mName, mType, mUnique, arg.mText, arg.mLength);
						return true;
					}
	
	string			mName;
	M6DataType		mType;
	bool			mUnique;
};

struct M6AttrExpr : public M6Expr
{
					M6AttrExpr(const string& inName) : mName(inName) {}
	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
					{
						inDocument->SetAttribute(mName, arg.mText, arg.mLength);
						return true;
					}

	string			mName;
};

struct M6StopExpr : public M6Expr
{
	virtual bool	Evaluate(M6InputDocument* inDocument, M6Argument& arg) const
					{
						return false;
					}
};

// --------------------------------------------------------------------

M6Processor::M6Processor(M6Databank& inDatabank, M6Lexicon& inLexicon,
		zx::element* inTemplate)
	: mDatabank(inDatabank), mLexicon(inLexicon), mConfig(inTemplate)
{
	zx::element* script = mConfig->find_first("script");
	if (script != nullptr)
		mScript = ParseScript(script);
}

M6Processor::~M6Processor()
{
}

M6ExprPtr M6Processor::ParseScript(zx::element* inScript)
{
	M6ListExpr* list = new M6ListExpr;
	foreach (zx::element* node, *inScript)
	{
		if (node->name() == "foreach")
		{
			M6ForeachExpr* foreach = new M6ForeachExpr(node->get_attribute("regex"));
			foreach->mLoop = ParseScript(node);
			list->mList.push_back(M6ExprPtr(foreach));
		}
		else if (node->name() == "split")
		{
			M6SplitExpr* split = new M6SplitExpr(node->get_attribute("separator"));
			split->mExpr = ParseScript(node);
			list->mList.push_back(M6ExprPtr(split));
		}
		else if (node->name() == "switch")
		{
			uint32 test = boost::lexical_cast<uint32>(node->get_attribute("test"));
			
			M6SwitchExpr* expr = new M6SwitchExpr(test);
			foreach (zx::element* n, *node)
			{
				if (n->name() == "case")
					expr->AddCase(n->get_attribute("value"), ParseScript(n));
				else if (n->name() == "default")
				{
					if (expr->mDefault)
						THROW(("<default> already defined in switch"));
					expr->mDefault = ParseScript(n);
				}
				else
					THROW(("only <case> and <default> are allowed in switch"));
			}

			list->mList.push_back(M6ExprPtr(expr));
		}
		else if (node->name() == "if")
		{
			if (not node->get_attribute("iteration").empty())
			{
				uint32 iteration = boost::lexical_cast<uint32>(node->get_attribute("iteration"));
				M6IfExpr* ifExpr = new M6IfExpr(iteration);
				ifExpr->mExpr = ParseScript(node);
				list->mList.push_back(M6ExprPtr(ifExpr));
			}
			else
				THROW(("unsupported if"));
		}
		else if (node->name() == "capture")
		{
			uint32 nr = boost::lexical_cast<uint32>(node->get_attribute("nr"));
			list->mList.push_back(M6ExprPtr(new M6CaptureExpr(nr, ParseScript(node))));
		}
		else if (node->name() == "to-lower")
			list->mList.push_back(M6ExprPtr(new M6TolowerExpr));
		else if (node->name() == "substr")
		{
			int32 start = 0, length = numeric_limits<int32>::max();
			if (not node->get_attribute("start").empty())
				start = boost::lexical_cast<uint32>(node->get_attribute("start"));
			if (not node->get_attribute("length").empty())
				length = boost::lexical_cast<uint32>(node->get_attribute("length"));
			list->mList.push_back(M6ExprPtr(new M6SubStrExpr(start, length)));
		}
		else if (node->name() == "replace")
			list->mList.push_back(M6ExprPtr(
				new M6ReplaceExpr(node->get_attribute("what"), node->get_attribute("with"))));
		else if (node->name() == "index")
			list->mList.push_back(M6ExprPtr(new M6IndexExpr(node->get_attribute("name"),
				MapDataType(node->get_attribute("type"), eM6TextData),
				node->get_attribute("unique") == "true")));
		else if (node->name() == "attr")
			list->mList.push_back(M6ExprPtr(new M6AttrExpr(node->get_attribute("name"))));
		else
			THROW(("Unsupported script element %s", node->name().c_str()));
	}
	
	return M6ExprPtr(list);
}

void M6Processor::ProcessFile(fs::path inFile)
{
	fs::ifstream file(inFile, ios::binary);
	string line;
	
	// do we need to strip off a header?
	zx::element* header = mConfig->find_first("header-line");
	if (header != nullptr)
	{
		boost::regex he(header->content());
		
		for (;;)
		{
			getline(file, line);
			if (line.empty() and file.eof())
				break;
			if (boost::regex_match(line, he))
				continue;
			break;
		}
	}
	else
		getline(file, line);

	string document;

	zx::element* separator = mConfig->find_first("document-separator");
	if (separator == nullptr)	// one file per document
	{
		io::filtering_ostream out(io::back_inserter(document));
		io::copy(file, out);
		ProcessDocument(document);
	}
	else
	{
		string separatorLine = separator->content();
		string separatorType = separator->get_attribute("type");
		
		if (separatorType == "first-line-equals" or separatorType.empty())
		{
			for (;;)
			{
				if (line == separatorLine)
				{
					if (not document.empty())
						ProcessDocument(document);
					document.clear();
				}
				
				document += line + "\n";

				getline(file, line);
				if (line.empty() and file.eof())
					break;
			}

			if (not document.empty())
				ProcessDocument(document);
		}
		else if (separatorType == "last-line-equals" or separatorType.empty())
		{
			for (;;)
			{
				document += line + "\n";
				if (line == separatorLine)
				{
					ProcessDocument(document);
					document.clear();
				}

				getline(file, line);
				if (line.empty() and file.eof())
					break;
			}

			if (not document.empty())
				cerr << "There was data after the last document" << endl;
		}
		else
			THROW(("Unknown document separator type"));
	}
}

void M6Processor::ProcessDocument(const string& inDocument)
{
	M6InputDocument* doc = new M6InputDocument(mDatabank, inDocument);
	
	M6Argument arg(inDocument.c_str(), inDocument.length());
	mScript->Evaluate(doc, arg);
	
	doc->Tokenize(mLexicon, 0);
	
	mDatabank.Store(doc);
}

// --------------------------------------------------------------------

M6Builder::M6Builder(const string& inDatabank)
	: mConfig(M6Config::Instance().LoadConfig(inDatabank))
	, mDatabank(nullptr)
{
}

M6Builder::~M6Builder()
{
	delete mDatabank;
}

void M6Builder::Glob(zx::element* inSource, vector<fs::path>& outFiles)
{
	if (inSource == nullptr)
		THROW(("No source specified for databank"));

	string source = inSource->content();
	ba::trim(source);
	
	if (inSource->get_attribute("type") == "path")
		outFiles.push_back(source);
	else
		THROW(("Unsupported source type"));
}

void M6Builder::Build()
{
//	boost::timer::auto_cpu_timer t;

	zx::element* file = mConfig->find_first("file");
	if (not file)
		THROW(("Invalid config-file, file is missing"));

	fs::path path = file->content();
	mDatabank = M6Databank::CreateNew(path.string());
	mDatabank->StartBatchImport(mLexicon);
	
	vector<fs::path> files;
	Glob(mConfig->find_first("source"), files);

	M6Processor processor(*mDatabank, mLexicon, mConfig);

	foreach (fs::path& file, files)
	{
		if (not fs::exists(file))
		{
			cerr << "file missing: " << file << endl;
			continue;
		}
		
		processor.ProcessFile(file);
	}
	
	cout << endl << "creating index..."; cout.flush();
	
	mDatabank->CommitBatchImport();
	cout << endl << "done" << endl;
}

