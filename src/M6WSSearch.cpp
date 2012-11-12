#include "M6Lib.h"

#include <iostream>

#include <zeep/dispatcher.hpp>

#include <boost/bind.hpp>
#include <boost/tr1/cmath.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "M6Databank.h"
#include "M6Server.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"
#include "M6Config.h"
#include "M6Query.h"
#include "M6Tokenizer.h"
#include "M6Builder.h"
#include "M6MD5.h"
#include "M6BlastCache.h"
#include "M6Exec.h"
#include "M6Parser.h"

#include "M6WSSearch.h"

using namespace std;
namespace ba = boost::algorithm;
namespace pt = boost::posix_time;

const string kSearchNS = "http://mrs.cmbi.ru.nl/mrsws/search";

// --------------------------------------------------------------------
//
//	M6 SOAP Search Server implementation
// 

M6WSSearch::M6WSSearch(const zeep::xml::element* inConfig)
	: zeep::server(kSearchNS, "mrsws_search")
	, M6SearchServer(inConfig)
{
	zx::element* addr = inConfig->find_first("external-address");
	if (addr != nullptr)
		set_location(addr->content());

	using namespace WSSearchNS;

	SOAP_XML_SET_STRUCT_NAME(FileInfo);
	SOAP_XML_SET_STRUCT_NAME(DatabankInfo);
	SOAP_XML_SET_STRUCT_NAME(Index);
	SOAP_XML_SET_STRUCT_NAME(Hit);
	SOAP_XML_SET_STRUCT_NAME(FindResult);
	SOAP_XML_SET_STRUCT_NAME(BooleanQuery);
	
	SOAP_XML_ADD_ENUM(Format, plain);
	SOAP_XML_ADD_ENUM(Format, title);
//	SOAP_XML_ADD_ENUM(Format, html);
	SOAP_XML_ADD_ENUM(Format, fasta);
	SOAP_XML_ADD_ENUM(Format, sequence);

	SOAP_XML_ADD_ENUM(IndexType, Unique);
	SOAP_XML_ADD_ENUM(IndexType, FullText);
	SOAP_XML_ADD_ENUM(IndexType, Number);
	SOAP_XML_ADD_ENUM(IndexType, Date);
	
	SOAP_XML_ADD_ENUM(Algorithm, Vector);
	SOAP_XML_ADD_ENUM(Algorithm, Dice);
	SOAP_XML_ADD_ENUM(Algorithm, Jaccard);
	
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, CONTAINS);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, LT);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, LE);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, EQ);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, GT);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, GE);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, UNION);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, INTERSECTION);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, NOT);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, ADJACENT);
	SOAP_XML_ADD_ENUM(BooleanQueryOperation, CONTAINSSTRING);

	const char* kGetDatabankInfoArgs[] = {
		"db", "info"
	};
	register_action(
		"GetDatabankInfo", this, &M6WSSearch::GetDatabankInfo, kGetDatabankInfoArgs);
	
	const char* kGetEntryArgs[] = {
		"db", "id", "format", "entry"
	};
	register_action(
		"GetEntry", this, &M6WSSearch::GetEntry, kGetEntryArgs);
	
	const char* kGetEntryLinesMatchingRegularExpressionArgs[] = {
		"db", "id", "regularExpression", "entry"
	};
	register_action(
		"GetEntryLinesMatchingRegularExpression", this, &M6WSSearch::GetEntryLinesMatchingRegularExpression, kGetEntryLinesMatchingRegularExpressionArgs);
	
	const char* kGetMetaDataArgs[] = {
		"db", "id", "meta", "data"
	};
	register_action(
		"GetMetaData", this, &M6WSSearch::GetMetaData, kGetMetaDataArgs);
	
	const char* kGetIndicesArgs[] = {
		"db", "indices"
	};
	register_action(
		"GetIndices", this, &M6WSSearch::GetIndices, kGetIndicesArgs);

	const char* kFindArgs[] = {
		"db", "queryterms", "algorithm", "alltermsrequired",
		"booleanfilter", "resultoffset", "maxresultcount", "response"
	};	
	register_action(
		"Find", this, &M6WSSearch::Find, kFindArgs);
	
	const char* kFindBooleanArgs[] = {
		"db", "query", "resultoffset", "maxresultcount", "response"
	};	
	register_action(
		"FindBoolean", this, &M6WSSearch::FindBoolean, kFindBooleanArgs);
	set_response_name("FindBoolean", "FindResponse");
	
//	const char* kFindSimilarArgs[] = {
//		"db", "id", "algorithm", "resultoffset", "maxresultcount", "response"
//	};
//	register_action(
//		"FindSimilar", this, &M6WSSearch::FindSimilar,
//		kFindSimilarArgs);
//	set_response_name("FindSimilar", "FindResponse");
	
	const char* kGetLinkedArgs[] = {
		"db", "id", "linkedDatabank", "resultoffset", "maxresultcount", "response"
	};
	register_action(
		"GetLinked", this, &M6WSSearch::GetLinked,
		kGetLinkedArgs);
	set_response_name("GetLinked", "FindResponse");
	
//	const char* kCountArgs[] = {
//		"db", "booleanquery", "response"
//	};
//	register_action(
//		"Count", this, &M6WSSearch::Count,
//		kCountArgs);
//
//	const char* kCooccurrenceArgs[] = {
//		"db", "ids", "idf_cutoff", "resultoffset", "maxresultcount", "terms"
//	};	
//	register_action(
//		"Cooccurrence", this, &M6WSSearch::Cooccurrence,
//		kCooccurrenceArgs);
//
//	const char* kSpellCheckArgs[] = {
//		"db", "queryterm", "suggestions"
//	};
//	register_action(
//		"SpellCheck", this, &M6WSSearch::SpellCheck,
//		kSpellCheckArgs);
//	
//	const char* kSuggestSearchTermsArgs[] = {
//		"db", "queryterm", "suggestions"
//	};
//	register_action(
//		"SuggestSearchTerms", this, &M6WSSearch::SuggestSearchTerms,
//		kSuggestSearchTermsArgs);
//
//	const char* kCompareDocumentsArgs[] = {
//		"db", "doc_a", "doc_b", "similarity"
//	};
//	register_action(
//		"CompareDocuments", this, &M6WSSearch::CompareDocuments,
//		kCompareDocumentsArgs);
//
//	const char* kClusterDocumentsArgs[] = {
//		"db", "ids", "response"
//	};
//	register_action(
//		"ClusterDocuments", this, &M6WSSearch::ClusterDocuments,
//		kClusterDocumentsArgs);

}

void M6WSSearch::GetDatabankInfo(const string& databank,
	vector<WSSearchNS::DatabankInfo>& info)
{
	log() << databank;

	foreach (M6LoadedDatabank& db, mLoadedDatabanks)
	{
		if (databank != "all" and db.mID != databank)
			continue;
		
		try
		{
			WSSearchNS::DatabankInfo dbInfo = { db.mID, db.mName };
			
//			dbInfo.url = ;
//			dbInfo.script = ;
//			dbInfo.blastable = ;
//			dbInfo.files;
//			dbInfo.links = ;
			
			info.push_back(dbInfo);
		}
		catch (exception& e)
		{
			log() << endl
				  << "Skipping db " << db.mID
				  << ": " << e.what();
		}
	}

	if (databank != "all" and info.size() == 0)
		THROW(("Unknown databank '%s'", databank.c_str()));
}

void M6WSSearch::GetIndices(const string& inDatabank, vector<WSSearchNS::Index>& outIndices)
{
	log() << inDatabank;

	M6Databank* db = Load(inDatabank);
	if (db == nullptr)
		THROW(("Databank %s not loaded", inDatabank.c_str()));
	
	M6DatabankInfo info;
	db->GetInfo(info);
	foreach (M6IndexInfo& ii, info.mIndexInfo)
	{
		WSSearchNS::Index ix = { ii.mName, "", ii.mCount };
		switch (ii.mType)
		{
			case eM6CharIndex:			ix.type = WSSearchNS::Unique; break;
			case eM6NumberIndex:		ix.type = WSSearchNS::Number; break;
			//case eM6DateIndex:			ix.type = WSSearchNS::Date; break;
			case eM6CharMultiIndex:		ix.type = WSSearchNS::Unique; break;
			case eM6NumberMultiIndex:	ix.type = WSSearchNS::Number; break;
			//case eM6DateMultiIndex:		ix.type = WSSearchNS::Date; break;
			case eM6CharMultiIDLIndex:	ix.type = WSSearchNS::Unique; break;
			case eM6CharWeightedIndex:	ix.type = WSSearchNS::FullText; break;
		}
		
		outIndices.push_back(ix);
	}
}

void M6WSSearch::GetEntry(const string& inDatabank, const string& inID,
	WSSearchNS::Format inFormat, string& outEntry)
{
	log() << inDatabank << ' ' << inID;
	
	M6Databank* db = Load(inDatabank);
	if (db == nullptr)
		THROW(("Databank %s not loaded", inDatabank.c_str()));
	
	switch (inFormat)
	{
		case WSSearchNS::plain:
			log() << ' ' << "plain";
			outEntry = M6SearchServer::GetEntry(db, "plain", "id", inID);
			break;
		
		case WSSearchNS::title:
			log() << ' ' << "title";
			outEntry = M6SearchServer::GetEntry(db, "title", "id", inID);
			break;
		
		case WSSearchNS::fasta:
			outEntry = M6SearchServer::GetEntry(db, "fasta", "id", inID);
			break;
		
		default:
			THROW(("Unsupported format in GetEntry"));
	}
}

void M6WSSearch::GetEntryLinesMatchingRegularExpression(
	const string& inDatabank, const string& inID, const string& inRE, string& outText)
{
	log() << inDatabank << ' ' << inID;
	
	M6Databank* db = Load(inDatabank);
	if (db == nullptr)
		THROW(("Databank %s not loaded", inDatabank.c_str()));
	
	istringstream s(M6SearchServer::GetEntry(db, "plain", "id", inID));
	ostringstream result;
	
	boost::regex re(inRE);
	
	for (;;)
	{
		string line;
		getline(s, line);
		
		if (s.eof())
			break;
		
		if (boost::regex_search(line, re))
			result << line << endl;
	}
	
	outText = result.str();
}

void M6WSSearch::GetMetaData(const string& inDatabank, const string& inID, const string& inMeta, string& outData)
{
	log() << inDatabank << ' ' << inID;
	
	M6Databank* db = Load(inDatabank);
	if (db == nullptr)
		THROW(("Databank %s not loaded", inDatabank.c_str()));

	unique_ptr<M6Iterator> iter(db->Find("id", inID));
	uint32 docNr;
	float rank;

	if (not (iter and iter->Next(docNr, rank)))
		THROW(("Entry %s not found", inID.c_str()));

	unique_ptr<M6Document> doc(db->Fetch(docNr));
	if (not doc)
		THROW(("Unable to fetch document %s", inID.c_str()));
	
	outData = doc->GetAttribute(inMeta);
}

void M6WSSearch::Find(const string& db, const vector<string>& queryterms,
	WSSearchNS::Algorithm algorithm, bool alltermsrequired, const string& booleanfilter,
	int resultoffset, int maxresultcount, vector<WSSearchNS::FindResult>& response)
{
	if (db == "all" or db == "*" or db.empty())
	{
		if (maxresultcount <= 0)
			maxresultcount = 5;

		foreach (M6LoadedDatabank& ldb, mLoadedDatabanks)
		{
			Find(ldb.mID, queryterms, algorithm, alltermsrequired, booleanfilter,
				resultoffset, maxresultcount, response);
		}
	}
	else if (M6Databank* databank = Load(db))
	{
		if (maxresultcount <= 0)
			maxresultcount = 15;

		M6Iterator* filter = nullptr;
		vector<string> terms;
			
		if (not booleanfilter.empty())
			ParseQuery(*databank, booleanfilter, alltermsrequired, terms, filter);
			
		unique_ptr<M6Iterator> iter(
			databank->Find(queryterms, filter, alltermsrequired, resultoffset + maxresultcount));
			
		if (iter)
		{
			WSSearchNS::FindResult result = { db, iter->GetCount() };
				
			uint32 docNr;
			float rank;
				
			while (resultoffset-- > 0 and iter->Next(docNr, rank))
				;
				
			while (maxresultcount-- > 0 and iter->Next(docNr, rank))
			{
				WSSearchNS::Hit h;
					
				unique_ptr<M6Document> doc(databank->Fetch(docNr));
					
				h.id = doc->GetAttribute("id");
				h.title = doc->GetAttribute("title");
				h.score = rank;
					
				result.hits.push_back(h);
			}

			response.push_back(result);
		}
	}
}

void ParseAdjacentQuery(const WSSearchNS::BooleanQuery& inQuery, vector<string>& outTerms)
{
	if (inQuery.operation == WSSearchNS::CONTAINS)
		outTerms.push_back(inQuery.value);
	else if (inQuery.operation == WSSearchNS::ADJACENT)
	{
		if (inQuery.leafs.size() != 2)
			THROW(("Adjacent should be used for two terms"));

		if (inQuery.leafs[0].operation != WSSearchNS::ADJACENT and inQuery.leafs[0].operation != WSSearchNS::CONTAINS)
			THROW(("Invalid adjacent query object, first parameter should be 'adjacent' or 'contains'"));

		if (inQuery.leafs[1].operation != WSSearchNS::CONTAINS)
			THROW(("Invalid adjacent query object, second parameter should be 'contains'"));
		
		if (inQuery.index != inQuery.leafs[0].index or inQuery.index != inQuery.leafs[1].index)
			THROW(("The index parameter should be the same for all adjacent object parameters"));

		ParseAdjacentQuery(inQuery.leafs[0], outTerms);
		ParseAdjacentQuery(inQuery.leafs[1], outTerms);
	}
}

M6Iterator* ParseQuery(M6Databank* inDatabank, const WSSearchNS::BooleanQuery& inQuery)
{
	M6Iterator* result = nullptr;
	
	if (ba::contains(inQuery.value, "*") or ba::contains(inQuery.value, "?"))
	{
		if (inQuery.operation != WSSearchNS::CONTAINS and inQuery.operation != WSSearchNS::EQ)
			THROW(("Invalid operation for matching pattern, only EQ and CONTAINS are allowed"));
		
		result = inDatabank->FindPattern(inQuery.index, inQuery.value);
	}
	else
	{
		switch (inQuery.operation)
		{
			case WSSearchNS::CONTAINS:	result = inDatabank->Find(inQuery.index, inQuery.value, eM6Contains);		break;
			case WSSearchNS::LT:		result = inDatabank->Find(inQuery.index, inQuery.value, eM6LessThan);		break;
			case WSSearchNS::LE:		result = inDatabank->Find(inQuery.index, inQuery.value, eM6LessOrEqual);	break;
			case WSSearchNS::EQ:		result = inDatabank->Find(inQuery.index, inQuery.value, eM6Equals);			break;
			case WSSearchNS::GE:		result = inDatabank->Find(inQuery.index, inQuery.value, eM6GreaterOrEqual);	break;
			case WSSearchNS::GT:		result = inDatabank->Find(inQuery.index, inQuery.value, eM6GreaterThan);	break;
			case WSSearchNS::NOT:		
				if (inQuery.leafs.size() != 1)
					THROW(("Only one parameter expected for NOT"));
				result = new M6NotIterator(ParseQuery(inDatabank, inQuery.leafs[0]), inDatabank->size());
				break;
			
			case WSSearchNS::UNION:
			{
				if (inQuery.leafs.size() < 1)
					THROW(("Please supply at least one subquery for a UNION"));
				
				unique_ptr<M6UnionIterator> iter(new M6UnionIterator());
				foreach (auto leaf, inQuery.leafs)
					iter->AddIterator(ParseQuery(inDatabank, leaf));
				result = iter.release();
				break;
			}
			
			case WSSearchNS::INTERSECTION:
			{
				if (inQuery.leafs.size() < 1)
					THROW(("Please supply at least one subquery for an INTERSECTION"));
				
				unique_ptr<M6IntersectionIterator> iter(new M6IntersectionIterator());
				foreach (auto leaf, inQuery.leafs)
					iter->AddIterator(ParseQuery(inDatabank, leaf));
				result = iter.release();
				break;
			}
			
			case WSSearchNS::ADJACENT:
			{
				vector<string> terms;
				ParseAdjacentQuery(inQuery, terms);
				result = inDatabank->FindString(inQuery.index, ba::join(terms, " "));
				break;
			}
	
			case WSSearchNS::CONTAINSSTRING:
				result = inDatabank->FindString(inQuery.index, inQuery.value);
				break;
			
			default:
				THROW(("Unrecognized query operation"));
		}
	}
	
	return result;
}

void M6WSSearch::FindBoolean(const string& inDatabank, const WSSearchNS::BooleanQuery& inQuery,
	int resultoffset, int maxresultcount, vector<WSSearchNS::FindResult>& response)
{
	log() << inDatabank;
	
	if (inDatabank == "*" or inDatabank == "all" or inDatabank == "")
	{
		if (maxresultcount > 5 or maxresultcount <= 0)
			maxresultcount = 3;

		foreach (M6LoadedDatabank& ldb, mLoadedDatabanks)
			FindBoolean(ldb.mID, inQuery, resultoffset, maxresultcount, response);
	}
	else if (M6Databank* databank = Load(inDatabank))
	{
		if (maxresultcount <= 0)
			maxresultcount = 15;

		unique_ptr<M6Iterator> iter(ParseQuery(databank, inQuery));
			
		if (iter)
		{
			WSSearchNS::FindResult result = { inDatabank, iter->GetCount() };
				
			uint32 docNr;
			float rank;
				
			while (resultoffset-- > 0 and iter->Next(docNr, rank))
				;
				
			while (maxresultcount-- > 0 and iter->Next(docNr, rank))
			{
				WSSearchNS::Hit h;
					
				unique_ptr<M6Document> doc(databank->Fetch(docNr));
					
				h.id = doc->GetAttribute("id");
				h.title = doc->GetAttribute("title");
				h.score = rank;
					
				result.hits.push_back(h);
			}

			response.push_back(result);
		}
	}
}

void M6WSSearch::GetLinked(const string& db, const string& id,
	const string& linkedDb, int resultoffset, int maxresultcount, vector<WSSearchNS::FindResult>& response)
{
	log() << db << "/" << id << " => " << linkedDb;

	M6Databank* msdb = Load(db);
	M6Databank* mddb = Load(linkedDb);
	
	if (msdb == nullptr or mddb == nullptr)
		THROW(("Databank not loaded"));
	
	// Collect the links
	unique_ptr<M6Iterator> iter(mddb->GetLinkedDocuments(db, id));

	if (iter)
	{
		if (maxresultcount <= 0)
			maxresultcount = 15;
		
		WSSearchNS::FindResult result = { linkedDb, iter->GetCount() };
			
		uint32 docNr;
		float rank;
			
		while (resultoffset-- > 0 and iter->Next(docNr, rank))
			;
			
		while (maxresultcount-- > 0 and iter->Next(docNr, rank))
		{
			WSSearchNS::Hit h;
				
			unique_ptr<M6Document> doc(mddb->Fetch(docNr));
				
			h.id = doc->GetAttribute("id");
			h.title = doc->GetAttribute("title");
			h.score = rank;
				
			result.hits.push_back(h);
		}

		response.push_back(result);
	}
}
