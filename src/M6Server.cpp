#include "M6Lib.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/tr1/cmath.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/random/random_device.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/program_options.hpp>

#include "M6Databank.h"
#include "M6Server.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"
#include "M6Config.h"
#include "M6Query.h"
#include "M6Tokenizer.h"
#include "M6MD5.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace pt = boost::posix_time;
namespace po = boost::program_options;

const string kM6ServerNS = "http://mrs.cmbi.ru.nl/mrs-web/ml";

int VERBOSE;

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
	
	boost::random::random_device rng;
	uint32 data[4] = { rng(), rng(), rng(), rng() };

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
	, mConfig(inConfig)
{
	string docroot = "docroot";
	zx::element* e = mConfig->find_first("docroot");
	if (e != nullptr)
		docroot = e->content();
	set_docroot(docroot);
	
	mount("",				boost::bind(&M6Server::handle_welcome, this, _1, _2, _3));
	mount("entry",			boost::bind(&M6Server::handle_entry, this, _1, _2, _3));
	mount("search",			boost::bind(&M6Server::handle_search, this, _1, _2, _3));
	mount("admin",			boost::bind(&M6Server::handle_admin, this, _1, _2, _3));
	mount("scripts",		boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("css",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("man",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("images",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("favicon.ico",	boost::bind(&M6Server::handle_file, this, _1, _2, _3));

	add_processor("entry",	boost::bind(&M6Server::process_mrs_entry, this, _1, _2, _3));
	add_processor("link",	boost::bind(&M6Server::process_mrs_link, this, _1, _2, _3));
	add_processor("redirect",
							boost::bind(&M6Server::process_mrs_redirect, this, _1, _2, _3));

	LoadAllDatabanks();
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

void M6Server::handle_welcome(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
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

void M6Server::handle_entry(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	string db, nr;

	zh::parameter_map params;
	get_parameters(scope, params);

	db = params.get("db", "").as<string>();
	nr = params.get("nr", "").as<string>();

	if (db.empty() or nr.empty())		// shortcut
	{
		handle_welcome(request, scope, reply);
		return;
	}
	
	uint32 docNr = boost::lexical_cast<uint32>(nr);

	string q, rq, format;
	q = params.get("q", "").as<string>();
	rq = params.get("rq", "").as<string>();
	format = params.get("format", "entry").as<string>();
	
	el::scope sub(scope);
	sub.put("db", el::object(db));
//	sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
	sub.put("nr", el::object(nr));
	if (not q.empty())
		sub.put("q", el::object(q));
	else if (not rq.empty())
	{
		q = rq;
		sub.put("redirect", el::object(rq));
		sub.put("q", el::object(rq));
	}
	sub.put("format", el::object(format));

//	vector<string> linked;
//	GetLinkedDbs(db, id, linked);
//	if (not linked.empty())
//	{
//		vector<el::object> links;
//		foreach (string& l, linked)
//			links.push_back(el::object(l));
//		sub.put("links", el::object(links));
//	}

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
		
		if (zx::element* script = format->find_first("script"))
		{
			string src = script->get_attribute("src");
			if (src.empty())
				sub["formatScript"] = script->content();
			else
				sub["formatSrc"] = src;
		}

		process_xml(root, sub, "/");	
		
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

		foreach (zx::element* link, format->find("link"))
		{
			try
			{
				string db = link->get_attribute("db");
				string id = link->get_attribute("id");
				string ix = link->get_attribute("ix");
				string anchor = link->get_attribute("anchor");
				if (db.empty() or id.empty())
					continue;
				boost::regex re(link->get_attribute("regex"));
				create_link_tags(root, re, db, ix, id, anchor);
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

			string db = inDatabank; if (db[0] == '$') db = m[atoi(db.c_str() + 1)];
			string id = inID;		if (id[0] == '$') id = m[atoi(id.c_str() + 1)];
			string ix = inIndex;	if (ix[0] == '$') ix = m[atoi(ix.c_str() + 1)];
//			string ix = node->get_attribute("index");			process_el(scope, ix);
			string an = inAnchor;	if (an[0] == '$') an = m[atoi(an.c_str() + 1)];
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
					
					Find(db.mDatabank, q, true, 0, 5, hits, c, r);
					
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
		
		M6Databank* mdb = Load(db);
		vector<el::object> hits;
		bool ranked;
		
		Find(mdb, q, true, resultoffset, maxresultcount, hits, hitCount, ranked);
		if (hitCount == 0)
		{
			sub.put("relaxed", el::object(true));
			Find(mdb, q, false, resultoffset, maxresultcount, hits, hitCount, ranked);
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
				unique_ptr<M6Iterator> rset(mdb->Find(ix, id, false));
				
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

void M6Server::LoadAllDatabanks()
{
	foreach (zx::element* db, mConfig->find("dbs/db"))
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
		if (not fs::exists(path))
		{
			if (VERBOSE)
				cerr << "databank " << databank << " not available" << endl;
			continue;
		}
		
		try
		{
			string name = databank;
			if (zx::element* n = db->find_first("name"))
				name = n->content();
			
			M6LoadedDatabank db =
			{
				new M6Databank(path, eReadOnly),
				databank,
				name
			};

			mLoadedDatabanks.push_back(db);
		}
		catch (exception& e)
		{
			cerr << "Error loading databank " << databank << endl
				 << " >> " << e.what() << endl;
		}
	}
}

M6Databank* M6Server::Load(const std::string& inDatabank)
{
	M6Databank* result = nullptr;

	foreach (M6LoadedDatabank& db, mLoadedDatabanks)
	{
		if (db.mID == inDatabank)
		{
			result = db.mDatabank;
			break;
		}
	}
	
	return result;
}

void M6Server::Find(M6Databank* inDatabank, const string& inQuery, bool inAllTermsRequired,
	uint32 inResultOffset, uint32 inMaxResultCount,
	vector<el::object>& outHits, uint32& outHitCount, bool& outRanked)
{
	unique_ptr<M6Iterator> rset;
	M6Iterator* filter;
	vector<string> queryTerms;
	
	ParseQuery(*inDatabank, inQuery, inAllTermsRequired, queryTerms, filter);
	if (queryTerms.empty())
		rset.reset(filter);
	else
		rset.reset(inDatabank->Find(queryTerms, filter, inAllTermsRequired, inResultOffset + inMaxResultCount));

	if (not rset or rset->GetCount() == 0)
		outHitCount = 0;
	else
	{
		outHitCount = rset->GetCount();
		outRanked = rset->IsRanked();
	
		uint32 docNr, nr = 1;
		
		float score = 0;

		while (inResultOffset-- > 0 and rset->Next(docNr, score))
			;

		while (inMaxResultCount-- > 0 and rset->Next(docNr, score))
		{
			unique_ptr<M6Document> doc(inDatabank->Fetch(docNr));
			
			el::object hit;
			hit["nr"] = nr;
			hit["docNr"] = docNr;
			hit["id"] = doc->GetAttribute("id");
			hit["title"] = doc->GetAttribute("title");
			hit["score"] = static_cast<uint16>(score * 100);
			
	//				vector<string> linked;
	//				GetLinkedDbs(db, id, linked);
	//				if (not linked.empty())
	//				{
	//					vector<el::object> links;
	//					foreach (string& l, linked)
	//						links.push_back(el::object(l));
	//					hit["links"] = links;
	//				}
			
			outHits.push_back(hit);
			++nr;
		}
	}		
}

uint32 M6Server::Count(const string& inDatabank, const string& inQuery)
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
		db->SuggestCorrection(inTerm, outCorrections);
	}
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

void RunMainLoop()
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
		
		unique_ptr<zeep::http::server> server(new M6Server(config));

		uint32 nrOfThreads = boost::thread::hardware_concurrency();

		server->bind(addr, boost::lexical_cast<uint16>(port));
		threads.create_thread(boost::bind(&zeep::http::server::run, server.get(), nrOfThreads));
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
		
		M6Config::SetConfigFile(configFile);
		
		RunMainLoop();
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

