#include "M6Lib.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/tr1/cmath.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/uuid_io.hpp>

#if BOOST_VERSION >= 104800
#include <boost/random/random_device.hpp>
#endif

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
#include "M6LinkTable.h"
#include "M6WSSearch.h"
#include "M6WSBlast.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace pt = boost::posix_time;
namespace po = boost::program_options;

const string kM6ServerNS = "http://mrs.cmbi.ru.nl/mrs-web/ml";

int VERBOSE;

// --------------------------------------------------------------------

M6SearchServer::M6SearchServer(const zx::element* inConfig)
	: mConfig(inConfig)
{
	LoadAllDatabanks();
}

M6SearchServer::~M6SearchServer()
{
}

void M6SearchServer::LoadAllDatabanks()
{
	fs::path mrsDir(M6Config::Instance().FindGlobal("/m6-config/mrsdir"));
	
	zx::element_set dbs(mConfig->find("dbs/db"));
	foreach (zx::element* db, dbs)
	{
		string databank = db->content();

		zx::element* config = M6Config::Instance().LoadDatabank(databank);
		if (not config)
		{
			if (VERBOSE)
				cerr << "unknown databank " << databank << endl;
			continue;
		}
		
		zx::element* file = config->find_first("file");
		if (file == nullptr)
		{
			if (VERBOSE)
				cerr << "file not specified for databank " << databank << endl;
			continue;
		}

		fs::path path = file->content();
		if (not path.has_root_path())
			path = mrsDir / path;
		
		if (not fs::exists(path))
		{
			if (VERBOSE)
				cerr << "databank " << databank << " not available" << endl;
			continue;
		}
		
		try
		{
			string name = databank;
			bool blast = false;
			
			M6Parser* parser = nullptr;
			zx::element_set config(M6Config::Instance().Find((boost::format("/m6-config/databank[@id='%1%']") % databank).str()));
			if (not config.empty())
			{
				parser = new M6Parser(config.front()->get_attribute("parser"));
				blast = config.front()->get_attribute("blast") == "true";
				if (zx::element* n = config.front()->find_first("name"))
					name = n->content();
			}
			
			M6LoadedDatabank ldb =
			{
				new M6Databank(path, eReadOnly),
				databank,
				name,
				blast,
				parser
			};

			mLoadedDatabanks.push_back(ldb);
		}
		catch (exception& e)
		{
			cerr << "Error loading databank " << databank << endl
				 << " >> " << e.what() << endl;
		}
	}
}

M6Databank* M6SearchServer::Load(const string& inDatabank)
{
	M6Databank* result = nullptr;

	string databank(inDatabank);
	M6Tokenizer::CaseFold(databank);

	foreach (M6LoadedDatabank& db, mLoadedDatabanks)
	{
		if (db.mID == databank)
		{
			result = db.mDatabank;
			break;
		}
	}
	
	return result;
}

string M6SearchServer::GetEntry(M6Databank* inDatabank, const string& inFormat, uint32 inDocNr)
{
	unique_ptr<M6Document> doc(inDatabank->Fetch(inDocNr));
	if (not doc)
		THROW(("Unable to fetch document"));
	
	string result;
	
	if (inFormat == "title")
		result = doc->GetAttribute("title");
	else
	{
		result = doc->GetText();
	
		if (inFormat == "fasta")
		{
			foreach (M6LoadedDatabank& db, mLoadedDatabanks)
			{
				if (db.mDatabank != inDatabank or db.mParser == nullptr)
					continue;
				
				string fasta;
				db.mParser->ToFasta(result, db.mID, doc->GetAttribute("id"), 
					doc->GetAttribute("title"), fasta);
				result = fasta;
				break;
			}
		}
	}
	
	return result;
}

string M6SearchServer::GetEntry(M6Databank* inDatabank,
	const string& inFormat, const string& inIndex, const string& inValue)
{
	unique_ptr<M6Iterator> iter(inDatabank->Find(inIndex, inValue));
	uint32 docNr;
	float rank;

	if (not (iter and iter->Next(docNr, rank)))
		THROW(("Entry %s not found", inValue.c_str()));

	return GetEntry(inDatabank, inFormat, docNr);
}

void M6SearchServer::Find(const string& inDatabank, const string& inQuery, bool inAllTermsRequired,
	uint32 inResultOffset, uint32 inMaxResultCount,
	vector<el::object>& outHits, uint32& outHitCount, bool& outRanked)
{
	M6Databank* databank = Load(inDatabank);

	if (databank == nullptr)
		THROW(("Invalid databank"));
	
	unique_ptr<M6Iterator> rset;
	M6Iterator* filter;
	vector<string> queryTerms;
	
	ParseQuery(*databank, inQuery, inAllTermsRequired, queryTerms, filter);
	if (queryTerms.empty())
		rset.reset(filter);
	else
		rset.reset(databank->Find(queryTerms, filter, inAllTermsRequired, inResultOffset + inMaxResultCount));

	if (not rset or rset->GetCount() == 0)
		outHitCount = 0;
	else
	{
		outHitCount = rset->GetCount();
		outRanked = rset->IsRanked();
	
		uint32 docNr, nr = 1;
		
		float score = 0;

		while (inResultOffset-- > 0 and rset->Next(docNr, score))
			++nr;

		while (inMaxResultCount-- > 0 and rset->Next(docNr, score))
		{
			unique_ptr<M6Document> doc(databank->Fetch(docNr));
			if (not doc)
				THROW(("Unable to fetch document %d", docNr));

			string id = doc->GetAttribute("id");

			el::object hit;
			hit["nr"] = nr;
			hit["docNr"] = docNr;
			hit["id"] = id;
			hit["title"] = doc->GetAttribute("title");
			hit["score"] = static_cast<uint16>(score * 100);
			
			vector<string> linked = M6LinkTable::Instance().GetLinkedDbs(inDatabank, id);
			if (not linked.empty())
			{
				vector<el::object> links;
				foreach (string& l, linked)
					links.push_back(el::object(l));
				hit["links"] = links;
			}
			
			outHits.push_back(hit);
			++nr;
		}
		
		if (inMaxResultCount == 0)
			outHitCount = nr - 1;
	}		
}

uint32 M6SearchServer::Count(const string& inDatabank, const string& inQuery)
{
	uint32 result = 0;
	
	if (inDatabank == "all")		// same as count for all databanks
	{
		foreach (M6LoadedDatabank& db, mLoadedDatabanks)
			result += Count(db.mID, inQuery);
	}
	else
	{
		unique_ptr<M6Iterator> rset;
		M6Iterator* filter;
		vector<string> queryTerms;
		
		M6Databank* db = Load(inDatabank);
		
		ParseQuery(*db, inQuery, true, queryTerms, filter);
		if (queryTerms.empty())
			rset.reset(filter);
		else
			rset.reset(db->Find(queryTerms, filter, true, 1));
		
		if (rset)
			result = rset->GetCount();
	}
	
	return result;
}

// --------------------------------------------------------------------

struct M6AuthInfo
{
						M6AuthInfo(const string& inRealm);
	
	bool				Validate(const string& inMethod, const string& inURI,
							const string& inHA1, map<string,string>& inInfo);
	string				GetChallenge() const;
	bool				Stale() const;
	
	string				mNonce, mRealm;
	uint32				mLastNC;
	pt::ptime			mCreated;
};

M6AuthInfo::M6AuthInfo(const string& inRealm)
	: mRealm(inRealm)
	, mLastNC(0)
{
	using namespace boost::gregorian;

#if BOOST_VERSION >= 104800
	boost::random::random_device rng;
	uint32 data[4] = { rng(), rng(), rng(), rng() };
#else
	int64 data[2] = { random(), random() };
#endif

	mNonce = M6MD5(data, sizeof(data)).Finalise();
	mCreated = pt::second_clock::local_time();
}

string M6AuthInfo::GetChallenge() const
{
	string challenge = "Digest ";
	challenge += "realm=\"" + mRealm + "\", qop=\"auth\", nonce=\"" + mNonce + '"';
	return challenge;
}

bool M6AuthInfo::Stale() const
{
	pt::time_duration age = pt::second_clock::local_time() - mCreated;
	return age.total_seconds() > 1800;
}

bool M6AuthInfo::Validate(const string& inMethod, const string& inURI,
	const string& inHA1, map<string,string>& inInfo)
{
	bool valid = false;
	
	uint32 nc = strtol(inInfo["nc"].c_str(), nullptr, 16);
	if (nc > mLastNC)
	{
		string ha2 = M6MD5(inMethod + ':' + inInfo["uri"]).Finalise();
		
		string response = M6MD5(
			inHA1 + ':' +
			inInfo["nonce"] + ':' +
			inInfo["nc"] + ':' +
			inInfo["cnonce"] + ':' +
			inInfo["qop"] + ':' +
			ha2).Finalise();
		
		valid = inInfo["response"] == response;
		mLastNC = nc;
	}
	return valid;
}

// --------------------------------------------------------------------

struct M6Redirect
{
	string	db;
	uint32	nr;
};

// --------------------------------------------------------------------

M6Server::M6Server(zx::element* inConfig)
	: webapp(kM6ServerNS)
	, M6SearchServer(inConfig)
{
	string docroot = "docroot";
	zx::element* e = mConfig->find_first("docroot");
	if (e != nullptr)
		docroot = e->content();
	set_docroot(docroot);
	
	mount("",				boost::bind(&M6Server::handle_welcome, this, _1, _2, _3));
	mount("download",		boost::bind(&M6Server::handle_download, this, _1, _2, _3));
	mount("entry",			boost::bind(&M6Server::handle_entry, this, _1, _2, _3));
	mount("link",			boost::bind(&M6Server::handle_link, this, _1, _2, _3));
	mount("search",			boost::bind(&M6Server::handle_search, this, _1, _2, _3));
	mount("similar",		boost::bind(&M6Server::handle_similar, this, _1, _2, _3));
	mount("admin",			boost::bind(&M6Server::handle_admin, this, _1, _2, _3));
	mount("scripts",		boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("css",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
//	mount("man",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("images",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("favicon.ico",	boost::bind(&M6Server::handle_file, this, _1, _2, _3));

	mount("blast",				boost::bind(&M6Server::handle_blast, this, _1, _2, _3));
	mount("ajax/blast/submit",	boost::bind(&M6Server::handle_blast_submit_ajax, this, _1, _2, _3));
	mount("ajax/blast/status",	boost::bind(&M6Server::handle_blast_status_ajax, this, _1, _2, _3));
	mount("ajax/blast/result",	boost::bind(&M6Server::handle_blast_results_ajax, this, _1, _2, _3));

	mount("align",				boost::bind(&M6Server::handle_align, this, _1, _2, _3));
	mount("ajax/align/submit",	boost::bind(&M6Server::handle_align_submit_ajax, this, _1, _2, _3));

	add_processor("entry",	boost::bind(&M6Server::process_mrs_entry, this, _1, _2, _3));
	add_processor("link",	boost::bind(&M6Server::process_mrs_link, this, _1, _2, _3));
	add_processor("redirect",
							boost::bind(&M6Server::process_mrs_redirect, this, _1, _2, _3));
}

void M6Server::init_scope(el::scope& scope)
{
	webapp::init_scope(scope);

	vector<el::object> databanks;
	foreach (M6LoadedDatabank& db, mLoadedDatabanks)
	{
		el::object databank;
		databank["id"] = db.mID;
		databank["name"] = db.mName;
		databanks.push_back(databank);
	}
	scope.put("databanks", el::object(databanks));
}

// --------------------------------------------------------------------

void M6Server::handle_request(const zh::request& req, zh::reply& rep)
{
	try
	{
		zh::webapp::handle_request(req, rep);
	}
	catch (zh::status_type& s)
	{
		rep = zh::reply::stock_reply(s);
	}
	catch (std::exception& e)
	{
		el::scope scope(req);
		scope.put("errormsg", el::object(e.what()));

		create_reply_from_template("error.html", scope, rep);
	}
}

void M6Server::create_unauth_reply(bool stale, zh::reply& rep)
{
	boost::mutex::scoped_lock lock(mAuthMutex);
	
	rep = zh::reply::stock_reply(zh::unauthorized);
	
	if (zx::element* realm = mConfig->find_first("realm"))
		mAuthInfo.push_back(new M6AuthInfo(realm->get_attribute("name")));
	else
		THROW(("Realm missing from config file"));
	
	string challenge = mAuthInfo.back()->GetChallenge();
	
	if (stale)
		challenge += ", stale=\"true\"";

	rep.set_header("WWW-Authenticate", challenge); 
}

void M6Server::handle_welcome(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	create_reply_from_template("index.html", scope, reply);
}

void M6Server::handle_file(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	fs::path file = get_docroot() / scope["baseuri"].as<string>();
	
	webapp::handle_file(request, scope, reply);
	
	if (file.extension() == ".html")
		reply.set_content_type("application/xhtml+xml");
}

void M6Server::handle_download(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	zh::parameter_map params;
	get_parameters(scope, params);

	string format = params.get("format", "entry").as<string>();

	string db = params.get("db", "").as<string>();

	M6Databank* mdb = Load(db);
	if (mdb == nullptr)
		THROW(("Databank %s not loaded", db.c_str()));
	
	string id;
	stringstream ss;
	uint32 n = 0;
	
	foreach (auto& p, params)
	{
		if (p.first == "id")
		{
			id = p.second.as<string>();
			ss << GetEntry(mdb, format, "id", id);
			++n;
		}
		else if (p.first == "nr")
		{
			ss << GetEntry(mdb, format, boost::lexical_cast<uint32>(p.second.as<string>()));
			++n;
		}
	}

	reply.set_content(ss.str(), "text/plain");
	
	if (n != 1 or id.empty())
		id = "mrs-data";

	reply.set_header("Content-disposition",
		(boost::format("attachement; filename=%1%.txt") % id).str());
}

void M6Server::handle_entry(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	string db, nr, id;

	zh::parameter_map params;
	get_parameters(scope, params);

	db = params.get("db", "").as<string>();
	nr = params.get("nr", "").as<string>();
	id = params.get("id", "").as<string>();

	if (db.empty() or (nr.empty() and id.empty()))		// shortcut
	{
		handle_welcome(request, scope, reply);
		return;
	}

	uint32 docNr = 0;
	if (nr.empty())
	{
		M6Databank* mdb = Load(db);
		if (mdb == nullptr)
			THROW(("Databank %s not loaded", db.c_str()));
		bool exists;
		tr1::tie(exists, docNr) = mdb->Exists("id", id);
		if (not exists)
			THROW(("Entry %s does not exist in databank %s", id.c_str(), db.c_str()));
		if (docNr == 0)
			THROW(("Multiple entries with ID %s in databank %s", id.c_str(), db.c_str()));

	}
	else
		docNr = boost::lexical_cast<uint32>(nr);
	
	string q, rq, format;
	q = params.get("q", "").as<string>();
	rq = params.get("rq", "").as<string>();
	format = params.get("format", "entry").as<string>();
	
	el::scope sub(scope);
	sub.put("db", el::object(db));
	sub.put("nr", el::object(docNr));
	sub.put("linkeddbs", el::object(M6LinkTable::Instance().GetLinkedDbs(db)));
	if (not q.empty())
		sub.put("q", el::object(q));
	else if (not rq.empty())
	{
		q = rq;
		sub.put("redirect", el::object(rq));
		sub.put("q", el::object(rq));
	}
	sub.put("format", el::object(format));

	vector<string> linked = M6LinkTable::Instance().GetLinkedDbs(db, id);
	if (not linked.empty())
	{
		vector<el::object> links;
		foreach (string& l, linked)
			links.push_back(el::object(l));
		sub.put("links", el::object(links));
	}

	zx::element* dbConfig = M6Config::Instance().LoadDatabank(db);
//	unique_ptr<M6Document> document(mdb->Fetch(docNr));

	// first stuff some data into scope
	
	el::object databank;
	databank["id"] = db;
	if (zx::element* name = dbConfig->find_first("name"))
		databank["name"] = name->content();
	else
		databank["name"] = db;
	if (zx::element* info = dbConfig->find_first("info"))
		databank["url"] = info->content();
	
//#ifndef NO_BLAST
//	databank["blastable"] = mNoBlast.count(db) == 0 and mdb->GetBlastDbCount() > 0;
//#endif
	sub.put("databank", databank);
//	sub.put("title", document->GetAttribute("title"));
	
	fs::ifstream data(get_docroot() / "entry.html");
	zx::document doc;
	doc.set_preserve_cdata(true);
	doc.read(data);
	
	zx::element* root = doc.child();

	try
	{
		zx::element* format = M6Config::Instance().LoadFormat(db);
		
		if (format != nullptr)
		{
			sub["formatXSLT"] = format->get_attribute("stylesheet");
			sub["formatScript"] = format->get_attribute("script");
		}
	
		process_xml(root, sub, "/");
		
		if (format != nullptr)
		{
			zx::element_set links(format->find("link"));
			foreach (zx::element* link, links)
			{
				try
				{
					string ldb = link->get_attribute("db");
					string id = link->get_attribute("id");
					string ix = link->get_attribute("ix");
					string anchor = link->get_attribute("anchor");
					if (ldb.empty())
						ldb = db;
					if (id.empty())
						continue;
					boost::regex re(link->get_attribute("regex"));
					create_link_tags(root, re, ldb, ix, id, anchor);
				}
				catch (...) {}
			}
		}
		
		if (not q.empty())
		{
			try
			{
				vector<string> terms;
				AnalyseQuery(q, terms);
				if (not terms.empty())
				{
					string pattern = ba::join(terms, "|");
					
					if (uc::contains_han(pattern))
						pattern = string("(") + pattern + ")";
					else
						pattern = string("\\b(") + pattern + ")\\b";
					
					boost::regex re(pattern, boost::regex_constants::icase);
					highlight_query_terms(root, re);
				}
			}
			catch (...) {}
		}

		reply.set_content(doc);
	}
	catch (M6Redirect& redirect)
	{
		create_redirect(redirect.db, redirect.nr, "", false, request, reply);
	}
}

void M6Server::highlight_query_terms(zx::element* node, boost::regex& expr)
{
	foreach (zx::element* e, *node)
		highlight_query_terms(e, expr);

	foreach (zx::node* n, node->nodes())
	{
		zx::text* text = dynamic_cast<zx::text*>(n);
		
		if (text == nullptr)
			continue;

		for (;;)
		{
			boost::smatch m;

			// somehow boost::regex_search works incorrectly with a const std::string...
#if defined(_MSC_VER)
			string s = text->str();
#else
			const string& s = text->str();
#endif
			if (not boost::regex_search(s, m, expr) or not m[0].matched or m[0].length() == 0)
				break;

			// split the text
			node->insert(text, new zx::text(m.prefix()));
			
			zx::element* span = new zx::element("span");
			span->add_text(m[0]);
			span->set_attribute("class", "highlight");
			node->insert(text, span);
			
			text->str(m.suffix());
		}
	}
}

void M6Server::create_link_tags(zx::element* node, boost::regex& expr,
	const string& inDatabank, const string& inIndex, const string& inID, const string& inAnchor)
{
	foreach (zx::element* e, *node)
		create_link_tags(e, expr, inDatabank, inIndex, inID, inAnchor);

	foreach (zx::node* n, node->nodes())
	{
		zx::text* text = dynamic_cast<zx::text*>(n);
		
		if (text == nullptr)
			continue;

		for (;;)
		{
			boost::smatch m;

			string s = text->str();
			// somehow boost::regex_search works incorrectly with a const std::string...
			if (not boost::regex_search(s, m, expr) or not m[0].matched or m[0].length() == 0)
				break;

			string db = inDatabank; if (ba::starts_with(db, "$")) db = m[atoi(db.c_str() + 1)];
			string id = inID;		if (ba::starts_with(id, "$")) id = m[atoi(id.c_str() + 1)];
			string ix = inIndex;	if (ba::starts_with(ix, "$")) ix = m[atoi(ix.c_str() + 1)];
//			string ix = node->get_attribute("index");			process_el(scope, ix);
			string an = inAnchor;	if (ba::starts_with(an, "$")) an = m[atoi(an.c_str() + 1)];
//			string title = node->get_attribute("title");		process_el(scope, title);
			string title;
			string q;
//			string q = node->get_attribute("q");				process_el(scope, q);
		
			bool exists = false;
			uint32 docNr = 0;
			
			zx::node* replacement = new zx::text(m[0]);
			
			try
			{
				M6Databank* mdb = Load(db);
				if (mdb != nullptr)
				{
					M6Tokenizer::CaseFold(id);
					
					tr1::tie(exists, docNr) = mdb->Exists(ix, id);
					
					unique_ptr<zx::element> a(new zeep::xml::element("a"));
					
					if (docNr != 0)
						a->set_attribute("href",
							(boost::format("entry?db=%1%&nr=%2%%3%%4%")
								% zeep::http::encode_url(db)
								% docNr
								% (q.empty() ? "" : ("&q=" + zeep::http::encode_url(q)).c_str())
								% (an.empty() ? "" : (string("#") + zeep::http::encode_url(an)).c_str())
							).str());
					else
					{
						a->set_attribute("href",
							(boost::format("link?db=%1%&ix=%2%&id=%3%%4%%5%")
								% zeep::http::encode_url(db)
								% zeep::http::encode_url(ix)
								% zeep::http::encode_url(id)
								% (q.empty() ? "" : ("&q=" + zeep::http::encode_url(q)).c_str())
								% (an.empty() ? "" : (string("#") + zeep::http::encode_url(an)).c_str())
							).str());
					
						if (not exists)
							a->set_attribute("class", "not-found");
					}
				
					if (not title.empty())
						a->set_attribute("title", title);
					
					a->append(replacement);
					replacement = a.release();
				}
			}
			catch (...) {}
			
			// split the text
			node->insert(text, new zx::text(m.prefix()));
			node->insert(text, replacement);
			text->str(m.suffix());
		}
	}
}

void M6Server::handle_search(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	string q, db, id, firstDb;
	uint32 page, hitCount = 0, firstDocNr = 0;
	
	zh::parameter_map params;
	get_parameters(scope, params);

	q = params.get("q", "").as<string>();
	db = params.get("db", "").as<string>();
	page = params.get("page", 1).as<uint32>();

	if (page < 1)
		page = 1;

	el::scope sub(scope);
	sub.put("page", el::object(page));
	sub.put("db", el::object(db));
	sub.put("q", el::object(q));

	if (db.empty() or q.empty() or (db == "all" and q == "*"))
		handle_welcome(request, scope, reply);
	else if (db == "all")
	{
		uint32 hits_per_page = params.get("count", 3).as<uint32>();
		if (hits_per_page > 5)
			hits_per_page = 5;

//		sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
		sub.put("count", el::object(hits_per_page));
	
		string hitDb = db;
		bool ranked = false;
		
		boost::thread_group thr;
		boost::mutex m;
		vector<el::object> databanks;
		
		foreach (M6LoadedDatabank& db, mLoadedDatabanks)
		{
			thr.create_thread([&]() {
				try
				{
					vector<el::object> hits;
					uint32 c;
					bool r;
					
					Find(db.mID, q, true, 0, 5, hits, c, r);
					
					boost::mutex::scoped_lock lock(m);
					
					hitCount += c;
					ranked = ranked or r;
					
					if (not hits.empty())
					{
						if (hitCount == c)
						{
							firstDb = db.mID;
							firstDocNr = hits.front()["docNr"].as<uint32>();
						}
					
						el::object databank;
						databank["id"] = db.mID;
						databank["name"] = db.mName;
						databank["hits"] = hits;
						databank["hitCount"] = c;
						databanks.push_back(databank);
					}
				}
				catch (...) { }
			});
		}
		thr.join_all();

		if (not databanks.empty())
		{
			sub.put("ranked", el::object(ranked));
			sub.put("hit-databanks", el::object(databanks));
		}
	}
	else
	{
		uint32 hits_per_page = params.get("show", 15).as<uint32>();
		if (hits_per_page > 100)
			hits_per_page = 100;
		
		sub.put("page", el::object(page));
		sub.put("db", el::object(db));
//		sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
		sub.put("q", el::object(q));
		sub.put("show", el::object(hits_per_page));

		int32 maxresultcount = hits_per_page, resultoffset = 0;
		
		if (page > 1)
			resultoffset = (page - 1) * maxresultcount;
		
		vector<el::object> hits;
		bool ranked;
		
		Find(db, q, true, resultoffset, maxresultcount, hits, hitCount, ranked);
		if (hitCount == 0)
		{
			sub.put("relaxed", el::object(true));
			Find(db, q, false, resultoffset, maxresultcount, hits, hitCount, ranked);
		}
		
		if (not hits.empty())
		{
			sub.put("hits", el::object(hits));
			if (hitCount == 1)
			{
				firstDb = db;
				firstDocNr = hits.front()["docNr"].as<uint32>();
			}
		}

		sub.put("first", el::object(resultoffset + 1));
		sub.put("last", el::object(resultoffset + hits.size()));
		sub.put("hitCount", el::object(hitCount));
		sub.put("lastPage", el::object(((hitCount - 1) / hits_per_page) + 1));
		sub.put("ranked", ranked);
	}

	vector<string> terms;
	AnalyseQuery(q, terms);
	if (not terms.empty())
	{
		// add some spelling suggestions
		sort(terms.begin(), terms.end());
		terms.erase(unique(terms.begin(), terms.end()), terms.end());
		
		vector<el::object> suggestions;
		foreach (string& term, terms)
		{
			try
			{
				boost::regex re(string("\\b") + term + "\\b");

				vector<pair<string,uint16>> s;
				SpellCheck(db, term, s);
				if (s.empty())
					continue;
				
				vector<el::object> alternatives;
				foreach (auto c, s)
				{
					el::object alt;
					alt["term"] = c.first;

					// construct new query, with the term replaced by the alternative
					ostringstream t;
					ostream_iterator<char, char> oi(t);
					boost::regex_replace(oi, q.begin(), q.end(), re, c.first,
						boost::match_default | boost::format_all);

//					if (Count(db, t.str()) > 0)
//					{
						alt["q"] = t.str();
						alternatives.push_back(alt);
//					}
				}
				
				if (alternatives.empty())
					continue;

				el::object so;
				so["term"] = term;
				so["alternatives"] = alternatives;
				
				suggestions.push_back(so);
			}
			catch (...) {}	// silently ignore errors
		}
			
		if (not suggestions.empty())
			sub.put("suggestions", el::object(suggestions));
	}

	// OK, now if we only have one hit, we might as well show it directly of course...
	if (hitCount == 1)
		create_redirect(firstDb, firstDocNr, q, true, request, reply);
	else
		create_reply_from_template(db == "all" ? "results-for-all.html" : "results.html",
			sub, reply);
}

void M6Server::handle_link(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	string id, db, ix, q;

	zeep::http::parameter_map params;
	get_parameters(scope, params);

	id = params.get("id", "").as<string>();
	db = params.get("db", "").as<string>();
	ix = params.get("ix", "").as<string>();
	q = params.get("q", "").as<string>();

	M6Tokenizer::CaseFold(db);
	M6Tokenizer::CaseFold(id);

	create_redirect(db, ix, id, q, false, request, reply);
}

void M6Server::handle_similar(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	string db, id, nr;
	uint32 page, hits_per_page = 15;

	zeep::http::parameter_map params;
	get_parameters(scope, params);

	db = params.get("db", "").as<string>();
	nr = params.get("nr", "").as<string>();
	page = params.get("page", 1).as<uint32>();
	
	int32 maxresultcount = hits_per_page, resultoffset = 0;
	
	if (page > 1)
		resultoffset = (page - 1) * maxresultcount;

	el::scope sub(scope);
	sub.put("page", el::object(page));
	sub.put("db", el::object(db));
//	sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
	sub.put("similar", el::object(nr));

	M6Databank* mdb = Load(db);
	if (mdb == nullptr) THROW(("Databank %s not loaded", db.c_str()));

	vector<string> queryTerms;
	M6Builder builder(db);
	builder.IndexDocument(GetEntry(mdb, "entry", boost::lexical_cast<uint32>(nr)), queryTerms);

	M6Iterator* filter = nullptr;
	unique_ptr<M6Iterator> results(mdb->Find(queryTerms, filter, false, resultoffset + maxresultcount));
	delete filter;

	if (results)
	{
		uint32 docNr, nr = 1, count = results->GetCount();
		float score;
		
		while (resultoffset-- > 0 and results->Next(docNr, score))
			++nr;
		
		vector<el::object> hits;
		sub.put("first", el::object(nr));
		
		while (maxresultcount-- > 0 and results->Next(docNr, score))
		{
			el::object hit;
			
			unique_ptr<M6Document> doc(mdb->Fetch(docNr));

			score *= 100;
			if (score > 100)
				score = 100;
			
			hit["nr"] = nr;
			hit["docNr"] = docNr;
			hit["id"] = doc->GetAttribute("id");
			hit["title"] = doc->GetAttribute("title");;
			hit["score"] = tr1::trunc(score);
			
//			vector<string> linked;
//			GetLinkedDbs(db, id, linked);
//			if (not linked.empty())
//			{
//				vector<el::object> links;
//				foreach (string& l, linked)
//					links.push_back(el::object(l));
//				hit["links"] = links;
//			}
			
			hits.push_back(hit);

			++nr;
		}
		
		if (maxresultcount > 0 and count + 1 > nr)
			count = nr - 1;
		
		sub.put("hits", el::object(hits));
		sub.put("hitCount", el::object(count));
		sub.put("lastPage", el::object(((count - 1) / hits_per_page) + 1));
		sub.put("last", el::object(nr - 1));
		sub.put("ranked", el::object(true));
	}
	
	create_reply_from_template("results.html", sub, reply);
}

void M6Server::handle_admin(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	ValidateAuthentication(request);
	create_reply_from_template("admin.html", scope, reply);
}

// --------------------------------------------------------------------

void M6Server::process_mrs_entry(zx::element* node, const el::scope& scope, fs::path dir)
{
	// evaluate attributes first
	foreach (zx::attribute& a, boost::iterator_range<zx::element::attribute_iterator>(node->attr_begin(), node->attr_end()))
	{
		string s = a.value();
		if (process_el(scope, s))
			a.value(s);
	}

	// fetch the entry
	string db = node->get_attribute("db");
	string nr = node->get_attribute("nr");
	string format = node->get_attribute("format");
	
	M6Databank* mdb = Load(db);
	if (mdb == nullptr)
		THROW(("databank not loaded"));
	unique_ptr<M6Document> doc(mdb->Fetch(boost::lexical_cast<uint32>(nr)));
	if (not doc)
		THROW(("Document %s not found", nr.c_str()));

	zx::node* replacement = nullptr;
	
	if (format == "title")
		replacement = new zx::text(doc->GetAttribute("title"));
	else // if (format == "plain")
	{
		zx::element* pre = new zx::element("pre");
		pre->set_attribute("id", "entrytext");
		pre->add_text(doc->GetText());
		replacement = pre;
	}
//#ifndef NO_BLAST
//	else if (format == "fasta")
//	{
//		zx::element* pre = new zx::element("pre");
//		pre->add_text(doc->GetText());
//		replacement = pre;
//	}
//#endif
//	else	// fall back to html
//	{
//		string entry = WFormat::Instance()->Format(mDbTable, db, id, "html");
//
//		try
//		{
//			// turn into XML... this is tricky
//			zx::document doc;
//			doc.set_preserve_cdata(true);
//			doc.read(entry);
//		
//			replacement = doc.child();
//			doc.root()->remove(replacement);
//		}
//		catch (exception& ex)
//		{
//			// parsing the pretty printed entry went wrong, try
//			// to display the plain text along with a warning
//			
//			zx::element* e = new zx::element("div");
//			
//			zx::element* m = new zx::element("div");
//			m->set_attribute("class", "format-error");
//			m->add_text("Error formatting entry: ");
//			m->add_text(ex.what());
//			e->push_back(m);
//			e->add_text("\n");
//			zx::comment* cmt = new zx::comment(entry);
//			e->push_back(cmt);
//			e->add_text("\n");
//			
//			zx::element* pre = new zx::element("pre");
//			pre->set_attribute("class", "format-error");
//			pre->add_text(mrsDb->GetDocument(id));
//			e->push_back(pre);
//			
//			replacement = e;
//		}
//	}
	
	if (replacement != nullptr)
	{
		zx::container* parent = node->parent();
		assert(parent);
		parent->insert(node, replacement);

		process_xml(replacement, scope, dir);
	}
}

void M6Server::process_mrs_link(zx::element* node, const el::scope& scope, fs::path dir)
{
	string db = node->get_attribute("db");				process_el(scope, db);
	string nr = node->get_attribute("nr");				process_el(scope, nr);
	string id = node->get_attribute("id");				process_el(scope, id);
	string ix = node->get_attribute("index");			process_el(scope, ix);
	string an = node->get_attribute("anchor");			process_el(scope, an);
	string title = node->get_attribute("title");		process_el(scope, title);
	string q = node->get_attribute("q");				process_el(scope, q);

	bool exists = false;
	
	if (nr.empty())
	{
		try
		{
			M6Databank* mdb = Load(db);
			if (mdb != nullptr)
			{
				unique_ptr<M6Iterator> rset(mdb->Find(ix, id));
				
				uint32 docNr, docNr2; float rank;
				if (rset and rset->Next(docNr, rank))
				{
					exists = true;
					if (not rset->Next(docNr2, rank))
						nr = boost::lexical_cast<string>(docNr);
				}
			}
		}
		catch (...) {}
	}
	
	zx::element* a = new zx::element("a");
	
	if (not nr.empty())
		a->set_attribute("href",
			(boost::format("entry?db=%1%&nr=%2%%3%%4%")
				% zh::encode_url(db)
				% zh::encode_url(nr)
				% (q.empty() ? "" : ("&q=" + zh::encode_url(q)).c_str())
				% (an.empty() ? "" : (string("#") + zh::encode_url(an)).c_str())
			).str());
	else
	{
		a->set_attribute("href",
			(boost::format("link?db=%1%&ix=%2%&id=%3%%4%%5%")
				% zh::encode_url(db)
				% zh::encode_url(ix)
				% zh::encode_url(id)
				% (q.empty() ? "" : ("&q=" + zh::encode_url(q)).c_str())
				% (an.empty() ? "" : (string("#") + zh::encode_url(an)).c_str())
			).str());
	
		if (not exists)
			a->set_attribute("class", "not-found");
	}

	if (not title.empty())
		a->set_attribute("title", title);

	zx::container* parent = node->parent();
	assert(parent);
	parent->insert(node, a);
	
	foreach (zx::node* c, node->nodes())
	{
		zx::node* clone = c->clone();
		a->push_back(clone);
		process_xml(clone, scope, dir);
	}
}

void M6Server::process_mrs_redirect(zx::element* node, const el::scope& scope, fs::path dir)
{
}

// --------------------------------------------------------------------

void M6Server::create_redirect(const string& databank, const string& inIndex, const string& inValue,
	const string& q, bool redirectForQuery, const zh::request& request, zh::reply& reply)
{
	string host = request.local_address;
	foreach (const zh::header& h, request.headers)
	{
		if (ba::iequals(h.name, "Host"))
		{
			host = h.value;
			break;
		}
	}

	// for some weird reason, local_port is sometimes 0 (bug in asio?)
	if (request.local_port != 80 and request.local_port != 0 and host.find(':') == string::npos)
	{
		host += ':';
		host += boost::lexical_cast<string>(request.local_port);
	}

	bool exists = false;
	M6Databank* mdb = Load(databank);

	if (mdb != nullptr)
	{
		uint32 docNr;
		tr1::tie(exists, docNr) = mdb->Exists(inIndex, inValue);
		if (exists)
		{
			string location;
			if (docNr != 0)
			{
				location =
					(boost::format("http://%1%/entry?db=%2%&nr=%3%&%4%=%5%")
						% host
						% zh::encode_url(databank)
						% docNr
						% (redirectForQuery ? "rq" : "q")
						% zh::encode_url(q)
					).str();
			}
			else
			{
				location =
					(boost::format("http://%1%/search?db=%2%&q=%3%:%4%")
						% host
						% zh::encode_url(databank)
						% inIndex
						% zh::encode_url(inValue)
					).str();
			}
	
			reply = zh::reply::redirect(location);
		}
	}

	if (not exists)
	{
		// ouch, nothing found... fall back to a lame page that says so
		// (this is an error, actually)
		
		el::scope scope(request);
		create_reply_from_template("results.html", scope, reply);
	}
}

void M6Server::create_redirect(const string& databank, uint32 inDocNr,
	const string& q, bool redirectForQuery, const zh::request& request, zh::reply& reply)
{
	string host = request.local_address;
	foreach (const zh::header& h, request.headers)
	{
		if (ba::iequals(h.name, "Host"))
		{
			host = h.value;
			break;
		}
	}

	// for some weird reason, local_port is sometimes 0 (bug in asio?)
	if (request.local_port != 80 and request.local_port != 0 and host.find(':') == string::npos)
	{
		host += ':';
		host += boost::lexical_cast<string>(request.local_port);
	}

	M6Databank* mdb = Load(databank);
	if (mdb == nullptr)
	{
		// ouch, nothing found... fall back to a lame page that says so
		// (this is an error, actually)
		
		el::scope scope(request);
		create_reply_from_template("results.html", scope, reply);
	}
	else
	{
		string location =
			(boost::format("http://%1%/entry?db=%2%&nr=%3%&%4%=%5%")
				% host
				% zh::encode_url(databank)
				% inDocNr
				% (redirectForQuery ? "rq" : "q")
				% zh::encode_url(q)
			).str();

		reply = zh::reply::redirect(location);
	}
}

// --------------------------------------------------------------------

void M6Server::SpellCheck(const string& inDatabank, const string& inTerm,
	vector<pair<string,uint16>>& outCorrections)
{
	if (inDatabank == "all")
	{
		vector<pair<string,uint16>> corrections;

		foreach (M6LoadedDatabank& db, mLoadedDatabanks)
		{
			vector<pair<string,uint16>> s;
			SpellCheck(db.mID, inTerm, s);
			if (not s.empty())
				corrections.insert(corrections.end(), s.begin(), s.end());
		}

		sort(corrections.begin(), corrections.end(),
			[](const pair<string,uint16>& a, const pair<string,uint16>& b) -> bool { return a.second > b.second; });
		
		set<string> words;
		foreach (auto c, corrections)
		{
			if (words.count(c.first))
				continue;
			outCorrections.push_back(c);
			words.insert(c.first);	
		}
	}
	else
	{
		M6Databank* db = Load(inDatabank);
		if (db != nullptr)
			db->SuggestCorrection(inTerm, outCorrections);
	}
}

// ====================================================================
// Blast

void M6Server::handle_blast(const zeep::http::request& request, const el::scope& scope, zeep::http::reply& reply)
{
	// default parameters
	string matrix = "BLOSUM62", expect = "10.0";
	int wordSize = 0, gapOpen = -1, gapExtend = -1, reportLimit = 250;
	bool filter = true, gapped = true;

	zeep::http::parameter_map params;
	get_parameters(scope, params);

	el::scope sub(scope);

	vector<el::object> databanks;
	foreach (M6LoadedDatabank& db, mLoadedDatabanks)
	{
		if (db.mBlast)
		{
			el::object databank;
			databank["id"] = db.mID;
			databank["name"] = db.mName;
			databanks.push_back(databank);
		}
	}
		
	// fetch some parameters, if any
	string db = params.get("db", "sprot").as<string>();
	string query = params.get("query", "").as<string>();
	
	sub.put("blastdatabanks", el::object(databanks));
	sub.put("blastdb", db);
	sub.put("query", query);

	const char* expectRange[] = { "0.001", "0.01", "0.1", "1.0", "10.0", "100.0", "1000.0" };
	sub.put("expectRange", expectRange, boost::end(expectRange));
	sub.put("expect", expect);

	uint32 wordSizeRange[] = { 0, 2, 3, 4 };
	sub.put("wordSizeRange", wordSizeRange, boost::end(wordSizeRange));
	sub.put("wordSize", wordSize);

	const char* matrices[] = { "BLOSUM45", "BLOSUM50", "BLOSUM62", "BLOSUM80", "BLOSUM90", "PAM30", "PAM70", "PAM250" };
	sub.put("matrices", matrices, boost::end(matrices));
	sub.put("matrix", matrix);

	int32 gapOpenRange[] = { -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
	sub.put("gapOpenRange", gapOpenRange, boost::end(gapOpenRange));
	sub.put("gapOpen", gapOpen);

	int32 gapExtendRange[] = { -1, 1, 2, 3, 4 };
	sub.put("gapExtendRange", gapExtendRange, boost::end(gapExtendRange));
	sub.put("gapExtend", gapExtend);

	uint32 reportLimitRange[] = { 100, 250, 500, 1000 };
	sub.put("reportLimitRange", reportLimitRange, boost::end(reportLimitRange));
	sub.put("reportLimit", reportLimit);

	sub.put("filter", filter);
	sub.put("gapped", gapped);

	create_reply_from_template("blast.html", sub, reply);
}

void M6Server::handle_blast_results_ajax(const zeep::http::request& request, const el::scope& scope, zeep::http::reply& reply)
{
	zeep::http::parameter_map params;
	get_parameters(scope, params);

	string id = params.get("job", "").as<string>();
	uint32 hitNr = params.get("hit", 0).as<uint32>();
	
	el::object result;
	
	M6BlastResultPtr job(M6BlastCache::Instance().JobResult(id));
	
//	// try to fetch the job
//	CBlastJobPtr job = CBlastJobProcessor::Instance()->Find(id);
//	const CBlastResult* result;
//	if (job != nullptr)
//		result = job->Result();
	
	if (not job)
		result["error"] = "Job expired";
//	else if (job->Status() != bj_Finished)
//		json << "{\"error\":\"Invalid job status\"}";
//	else if (result == nullptr)
//		json << "{\"error\":\"Internal error (no result?)\"}";
	else if (hitNr > 0)	// we need to return the hsps for this hit
	{
		const list<M6Blast::Hit>& hits(job->mHits);
		if (hitNr > hits.size())
			THROW(("Hitnr out of range"));
		
		list<M6Blast::Hit>::const_iterator hit = hits.begin();
		advance(hit, hitNr - 1);
		
		const list<M6Blast::Hsp>& hsps(hit->mHsps);
		
		vector<el::object> jhsps;
		
		foreach (const M6Blast::Hsp& hsp, hsps)
		{
			string queryAlignment = hsp.mQueryAlignment;
			
			// calculate the offsets for the graphical representation of this hsp
	        uint32 qf = hsp.mQueryStart;
	        uint32 qt = hsp.mQueryEnd;
	        uint32 ql = job->mQueryLength;
	
	        uint32 sf = hsp.mTargetStart;
	        uint32 st = hsp.mTargetEnd;
	        uint32 sl = hsp.mTargetLength;
	
	        uint32 length = static_cast<uint32>(queryAlignment.length());
	        uint32 before = qf;
	        if (before < sf)
	            before = sf;
	
	        uint32 after = ql - qt;
	        if (after < sl - st)
	            after = sl - st;
	
	        length += before + after;
	
	        float factor = 150;
	        factor /= length;
	
			uint32 ql1, ql2, ql3, ql4, sl1, sl2, sl3, sl4;
	
	        if (qf < before)
	            ql1 = uint32(factor * (before - qf));
	        else
	            ql1 = 0;
	        ql2 = uint32(factor * qf);
	        ql3 = uint32(factor * queryAlignment.length());
	        ql4 = uint32(factor * (ql - qt));
	
	        if (sf < before)
	            sl1 = uint32(factor * (before - sf));
	        else
	            sl1 = 0;
	        sl2 = uint32(factor * sf);
	        sl3 = uint32(factor * queryAlignment.length());
	        sl4 = uint32(factor * (sl - st));
	
	        if (ql1 > 0 and ql1 + ql2 < sl2)
	            ql1 = sl2 - ql2;
	
	        if (sl1 > 0 and sl1 + sl2 < ql2)
	            sl1 = ql2 - sl2;

			el::object h;
			h["nr"] = jhsps.size() + 1;
			h["score"] = hsp.mScore;
			h["bitScore"] = hsp.mBitScore;
			h["expect"] = hsp.mExpect;
			h["queryAlignment"] = queryAlignment;
			h["queryStart"] = hsp.mQueryStart;
			h["subjectAlignment"] = hsp.mTargetAlignment;
			h["subjectStart"] = hsp.mTargetStart;
			h["midLine"] = hsp.mMidLine;
			h["identity"] = hsp.mIdentity;
			h["positive"] = hsp.mPositive;
			h["gaps"] = hsp.mGaps;
			
			vector<el::object> qls(4);
			qls[0] = ql1; qls[1] = ql2; qls[2] = ql3; qls[3] = ql4;
			h["ql"] = qls;
			
			vector<el::object> sls(4);
			sls[0] = sl1; sls[1] = sl2; sls[2] = sl3; sls[3] = sl4;
			h["sl"] = sls;
			
			jhsps.push_back(h);
		}

		result = jhsps;
	}
	else
	{
		const list<M6Blast::Hit>& hits(job->mHits);
		
		vector<el::object> jhits;
		
		foreach (const M6Blast::Hit& hit, hits)
		{
			const list<M6Blast::Hsp>& hsps(hit.mHsps);
			if (hsps.empty())
				continue;
			const M6Blast::Hsp& best = hsps.front();

			uint32 coverageStart = 0, coverageLength = 0, coverageColor = 1;
			float bin = (best.mPositive * 4.0f) / best.mQueryAlignment.length();

			if (bin >= 3.5)
				coverageColor = 1;
			else if (bin >= 3)
				coverageColor = 2;
			else if (bin >= 2.5)
				coverageColor = 3;
			else if (bin >= 2)
				coverageColor = 4;
			else
				coverageColor = 5;

			float queryLength = job->mQueryLength;
			const int kGraphicWidth = 100;

			coverageStart = uint32(best.mQueryStart * kGraphicWidth / queryLength);
			coverageLength = uint32(best.mQueryAlignment.length() * kGraphicWidth / queryLength);
			
			el::object h;
			h["nr"] = jhits.size() + 1;
			h["db"] = hit.mDb;
			h["doc"] = hit.mID;
			h["seq"] = hit.mChain;
			h["desc"] = hit.mTitle;
			h["bitScore"] = best.mBitScore;
			h["expect"] = best.mExpect;
			h["hsps"] = hsps.size();
			
			el::object coverage;
			coverage["start"] = coverageStart;
			coverage["length"] = coverageLength;
			coverage["color"] = coverageColor;
			h["coverage"] = coverage;
			
			jhits.push_back(h);
		}

		result = jhits;
	}
	
	reply.set_content(result.toJSON(), "text/javascript");
}

ostream& operator<<(ostream& os, M6BlastJobStatus status)
{
	switch (status)
	{
		case bj_Unknown:	os << "unknown"; break;
		case bj_Queued:		os << "queued"; break;
		case bj_Running:	os << "running"; break;
		case bj_Finished:	os << "finished"; break;
		case bj_Error:		os << "error"; break;
	}
	
	return os;
}

void M6Server::handle_blast_status_ajax(const zeep::http::request& request, const el::scope& scope, zeep::http::reply& reply)
{
	zeep::http::parameter_map params;
	get_parameters(scope, params);

	string ids = params.get("jobs", "").as<string>();
	vector<string> jobs;
	
	if (not ids.empty())
		ba::split(jobs, ids, ba::is_any_of(";"));

	vector<el::object> jjobs;
	
	foreach (const string& id, jobs)
	{
		try
		{
			M6BlastJobStatus status;
			string error;
			uint32 hitCount;
			double bestScore;
			
			tr1::tie(status, error, hitCount, bestScore) = M6BlastCache::Instance().JobStatus(id);
			
			el::object jjob;
			jjob["id"] = id;
			jjob["status"] = boost::lexical_cast<string>(status);
			
			switch (status)
			{
				case bj_Finished:
					jjob["hitCount"] = hitCount;
					jjob["bestEValue"] = bestScore;
					break;
				
				case bj_Error:
					jjob["error"] = error;
					break;
			}
			
			jjobs.push_back(jjob);
		}
		catch (...) {}
	}
	
	el::object json(jjobs);
	reply.set_content(json.toJSON(), "text/javascript");
}

void M6Server::handle_blast_submit_ajax(
	const zeep::http::request&	request,
	const el::scope&			scope,
	zeep::http::reply&			reply)
{
	// default parameters
	string id, db, matrix, expect, query, program;
	int wordSize = 0, gapOpen = -1, gapExtend = -1, reportLimit = 250;
	bool filter = true, gapped = true;

	zeep::http::parameter_map params;
	get_parameters(scope, params);

	// fetch the parameters
	id = params.get("id", "").as<string>();			// id is used by the client
	db = params.get("db", "pdb").as<string>();
	matrix = params.get("matrix", "BLOSUM62").as<string>();
	expect = params.get("expect", "10.0").as<string>();
	query = params.get("query", "").as<string>();
	program = params.get("program", "blastp").as<string>();

	wordSize = params.get("wordSize", wordSize).as<int>();
	gapped = params.get("gapped", true).as<bool>();
	gapOpen = params.get("gapOpen", gapOpen).as<int>();
	gapExtend = params.get("gapExtend", gapExtend).as<int>();
	reportLimit = params.get("reportLimit", reportLimit).as<int>();
	filter = params.get("filter", true).as<bool>();
	
	// first parse the query (in case it is in FastA format)
	ba::replace_all(query, "\r\n", "\n");
	ba::replace_all(query, "\r", "\n");
	istringstream is(query);
	string qid;

	el::object result;
	result["clientId"] = id;
	
	try
	{
		query.clear();
		for (;;)
		{
			string line;
			getline(is, line);
			if (line.empty() and is.eof())
				break;
			
			if (qid.empty() and ba::starts_with(line, ">"))
			{
				qid = line.substr(1);
				continue;
			}
			
			ba::to_upper(line);
			query += line;
		}

			// validate the sequence
		if (program == "blastn" or program == "blastx" or program == "tblastx")
		{
			if (not ba::all(query, ba::is_any_of("ACGT")))
			{
				PRINT(("Error in parameters:\n%s", request.payload.c_str()));
				THROW(("not a valid sequence"));
			}
		}
		else
		{
			if (not ba::all(query, ba::is_any_of("LAGSVETKDPIRNQFYMHCWBZXU")))
			{
				PRINT(("Error in parameters:\n%s", request.payload.c_str()));
				THROW(("not a valid sequence"));
			}
		}
		
//		CDatabankPtr mrsDb = mDbTable[db];
//		
//		// validate the program/query/db combo
//		if (mrsDb->GetBlastDbSeqType() != ((program == "blastn" or program == "tblastn" or program == "tblastx") ? 'N' : 'P'))
//			THROW(("Invalid databank for blast program"));
//		
//		CBlastJobPtr job = CBlastJobProcessor::Instance()->Submit(
//			mrsDb, "", query, program, matrix, wordSize,
//			boost::lexical_cast<double>(expect), filter,
//			gapped, gapOpen, gapExtend, reportLimit);

		string jobId = M6BlastCache::Instance().Submit(
			db, query, matrix, wordSize,
			boost::lexical_cast<double>(expect), filter,
			gapped, gapOpen, gapExtend, reportLimit);
	
		// and answer with the created job ID
		result["id"] = jobId;
		result["qid"] = qid;
		result["status"] = boost::lexical_cast<string>(bj_Queued);
	}
	catch (exception& e)
	{
		result["status"] = boost::lexical_cast<string>(bj_Error);
		result["error"] = e.what();
	}
	
	reply.set_content(result.toJSON(), "text/javascript");
}

void M6Server::handle_align(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	zeep::http::parameter_map params;
	get_parameters(scope, params);

	el::scope sub(scope);

	string seqstr = params.get("seqs", "").as<string>();
	
	ba::replace_all(seqstr, "\r\n", "\n");
	ba::replace_all(seqstr, "\r", "\n");
	
	map<string,shared_ptr<M6Parser>> parsers;
	
	if (not seqstr.empty())
	{
		vector<string> seqs;
		ba::split(seqs, seqstr, ba::is_any_of(";"));
		string fasta;
		
		foreach (string& ts, seqs)
		{
			vector<string> t;
			ba::split(t, ts, ba::is_any_of("/"));
			
			if (t.size() < 2 or t.size() > 3)
				THROW(("Invalid parameters passed for align"));
			
			M6Databank* mdb = Load(t[0]);
			if (mdb == nullptr)
				THROW(("Databank %s not loaded", t[0].c_str()));
			fasta += GetEntry(mdb, "fasta", "id", t[1]);

//			CDatabankPtr db = mDbTable[t[0]];
//			uint32 docNr = db->GetDocumentNr(t[1]);
//			uint32 seqNr = 0;
//			if (t.size() == 3)
//				seqNr = db->GetSequenceNr(docNr, t[2]);
//			
//			string seq;
//			db->GetSequence(docNr, seqNr, seq);
//			for (uint32 o = 72; o < seq.length(); o += 73)
//				seq.insert(seq.begin() + o, '\n');
//			
//			if (t.size() == 2)
//				fastaStream << '>' << t[1] << endl << seq << endl;
//			else
//				fastaStream << '>' << t[1] << '.' << t[2] << endl << seq << endl;
		}

		sub.put("input", el::object(fasta));
	}
	
	create_reply_from_template("align.html", sub, reply);
}

void M6Server::handle_align_submit_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	zeep::http::parameter_map params;
	get_parameters(scope, params);
	
	el::object result;

	string fasta = params.get("input", "").as<string>();
	if (fasta.empty())
		result["error"] = "No input specified for align";
	else
	{
		try
		{
			string clustalo = M6Config::Instance().FindGlobal("/m6-config/tools/tool[@name='clustalo']");
			if (clustalo.empty())
				clustalo = "/usr/bin/clustalo";
			
			vector<const char*> args;
			args.push_back(clustalo.c_str());
			args.push_back("-i");
			args.push_back("-");
			args.push_back(nullptr);

			double maxRunTime = 30;
			string out, err;
			
			int r = ForkExec(args, maxRunTime, fasta, out, err);

			result["alignment"] = out;
			if (not err.empty())
				result["error"] = err;
		}
		catch (exception& e)
		{
			result["error"] = e.what();
		}
	}

	reply.set_content(result.toJSON(), "text/javascript");
}

void M6Server::ValidateAuthentication(const zh::request& request)
{
	string authorization;
	foreach (const zeep::http::header& h, request.headers)
	{
		if (ba::iequals(h.name, "Authorization"))
			authorization = h.value;
	}

	if (authorization.empty())
		throw zh::unauthorized_exception(false);

	// That was easy, now check the response
	
	map<string,string> info;
	
	boost::regex re("(\\w+)=(?|\"([^\"]*)\"|'([^']*)'|(\\w+))(?:,\\s*)?");
	const char* b = authorization.c_str();
	const char* e = b + authorization.length();
	boost::match_results<const char*> m;
	while (b < e and boost::regex_search(b, e, m, re))
	{
		info[string(m[1].first, m[1].second)] = string(m[2].first, m[2].second);
		b = m[0].second;
	}

	bool authorized = false, stale = false;

	boost::format f("realm[@name='%1%']/user[@name='%2%']");
	if (zx::element* user = mConfig->find_first((f % info["realm"] % info["username"]).str()))
	{
		string ha1 = user->content();

		boost::mutex::scoped_lock lock(mAuthMutex);
	
		foreach (M6AuthInfo* auth, mAuthInfo)
		{
			if (auth->mRealm == info["realm"] and auth->mNonce == info["nonce"]
				and auth->Validate(request.method, request.uri, ha1, info))
			{
				authorized = true;
				stale = auth->Stale();
				if (stale)
					mAuthInfo.erase(find(mAuthInfo.begin(), mAuthInfo.end(), auth));
				break;
			}
		}
	}
	
	if (stale or not authorized)
		throw zh::unauthorized_exception(stale);
}

// --------------------------------------------------------------------

void RunMainLoop(uint32 inNrOfThreads)
{
	cout << "Restarting services..."; cout.flush();
	
	vector<zeep::http::server*> servers;
	boost::thread_group threads;

	foreach (zx::element* config, M6Config::Instance().LoadServers())
	{
		string addr = config->get_attribute("addr");
		string port = config->get_attribute("port");
		if (port.empty())
			port = "80";
		
		if (VERBOSE)
			cout << "listening at " << addr << ':' << port << endl;
		
		string service = config->get_attribute("type");
		unique_ptr<zeep::http::server> server;
		
		if (service == "www" or service.empty())
			server.reset(new M6Server(config));
		else if (service == "search")
			server.reset(new M6WSSearch(config));
		else if (service == "blast")
			server.reset(new M6WSBlast(config));
		else
			THROW(("Unknown service %s", service.c_str()));

		server->bind(addr, boost::lexical_cast<uint16>(port));
		threads.create_thread(boost::bind(&zeep::http::server::run, server.get(), inNrOfThreads));
		servers.push_back(server.release());
	}

	if (servers.empty())
	{
		cerr << "No servers configured" << endl;
		exit(1);
	}

	if (not VERBOSE)
		cout << " done" << endl;
	
	threads.join_all();

	foreach (zeep::http::server* server, servers)
		delete server;
}

int main(int argc, char* argv[])
{
	try
	{
		po::options_description desc("m6-server");
		desc.add_options()
			("config-file,c", po::value<string>(),	"Configuration file")
			("verbose,v",							"Be verbose")
			("threads,a", po::value<uint32>(),		"Nr of threads")
			("help,h",								"Display help message")
			;
	
		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
		po::notify(vm);
	
		if (vm.count("help"))
		{
			cout << desc << "\n";
			exit(1);
		}
		
		if (vm.count("verbose"))
			VERBOSE = 1;

		fs::path configFile("config/m6-config.xml");
		if (vm.count("config-file"))
			configFile = vm["config-file"].as<string>();
		
		if (not fs::exists(configFile))
			THROW(("Configuration file not found (\"%s\")", configFile.string().c_str()));

		uint32 nrOfThreads = boost::thread::hardware_concurrency();
		if (vm.count("threads"))
			nrOfThreads = vm["threads"].as<uint32>();

		M6Config::SetConfigFile(configFile);
		
		RunMainLoop(nrOfThreads);
	}
	catch (exception& e)
	{
		cerr << endl
			 << "m6-builder exited with an exception:" << endl
			 << e.what() << endl;
		exit(1);
	}
	catch (...)
	{
		cerr << endl
			 << "m6-builder exited with an uncaught exception" << endl;
		exit(1);
	}
	
	return 0;
}

