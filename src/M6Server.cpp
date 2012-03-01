#include "M6Lib.h"

#include <boost/bind.hpp>
#include <boost/tr1/cmath.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Databank.h"
#include "M6Server.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"
#include "M6Config.h"

using namespace std;
namespace fs = boost::filesystem;

const string kM6ServerNS = "http://mrs.cmbi.ru.nl/mrs-web/ml";

// --------------------------------------------------------------------

M6Server::M6Server(zeep::xml::element* inConfig)
	: webapp(kM6ServerNS)
	, mConfig(inConfig)
{
	string docroot = "docroot";
	zeep::xml::element* e = mConfig->find_first("docroot");
	if (e != nullptr)
		docroot = e->content();
	set_docroot(docroot);
	
	mount("",				boost::bind(&M6Server::handle_welcome, this, _1, _2, _3));
	mount("entry",			boost::bind(&M6Server::handle_entry, this, _1, _2, _3));
	mount("search",			boost::bind(&M6Server::handle_search, this, _1, _2, _3));
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
		databank["id"] = db.mName; //dbi.GetID();
		databank["name"] = db.mName; // db->GetName();
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
	handle_welcome(request, scope, reply);
}

void M6Server::handle_search(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	string q, db, id, firstId;
	uint32 page, hitCount = 0;
	
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

	// store the query terms
	vector<string> queryTerms;

	if (db.empty() or q.empty() or (db == "all" and q == "*"))
		handle_welcome(request, scope, reply);
	else if (db == "all")
	{
		uint32 hits_per_page = params.get("count", 3).as<uint32>();
		if (hits_per_page > 5)
			hits_per_page = 5;
		
//		sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
		sub.put("show", el::object(hits_per_page));
	
		string hitDb = db;
		bool ranked = false;

		vector<el::object> databanks;
		
		foreach (M6LoadedDatabank& db, mLoadedDatabanks)
		{
//			if (mIgnoreInAll.count(dbi.GetID()))
//				continue;

			int32 maxresultcount = hits_per_page;

			el::object databank;
			databank["id"] = db.mName;
			databank["name"] = db.mName; // mrsDb->GetName();
			
			unique_ptr<M6Iterator> rset(db.mDatabank->Find(q, true, maxresultcount));
			if (not rset)
				continue;
			
//			auto_ptr<CDocIterator> results(PerformSearch(*dbi, q, maxresultcount, true, count, ranked, queryTerms));
	
			uint32 docNr, nr = 1;
			
			vector<el::object> hits;
			sub.put("first", el::object(nr));
			
			float score = 0;
			while (maxresultcount-- > 0 and rset->Next(docNr, score))
			{
				unique_ptr<M6Document> doc(db.mDatabank->Fetch(docNr));
				
				el::object hit;
	
				hit["nr"] = nr;
				hit["docNr"] = docNr;
				hit["id"] = doc->GetAttribute("id");
				hit["title"] = doc->GetAttribute("title");
				
				if (score > 1.f)
					score = 1.f;
				
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
				
				hits.push_back(hit);
				++nr;
			}
			
			if (not hits.empty())
			{
				databank["hits"] = hits;
				databank["hitCount"] = rset->GetCount();
				databanks.push_back(databank);
				
				hitCount += rset->GetCount();
			}
		}

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
		bool ranked = false;
		
		if (page > 1)
			resultoffset = (page - 1) * maxresultcount;
		
		M6Databank* mdb = Load(db);
		unique_ptr<M6Iterator> rset(mdb->Find(q, true, page * maxresultcount));
		if (not rset)
		{
			rset.reset(mdb->Find(q, false, page * maxresultcount));
			sub.put("relaxed", el::object(true));
		}

		if (rset)
		{
			uint32 docNr, nr = 1;
			float score;
			while (resultoffset-- > 0 and rset->Next(docNr, score))
				++nr;
			
			vector<el::object> hits;
			sub.put("first", el::object(nr));
			
			while (maxresultcount-- > 0 and rset->Next(docNr, score))
			{
				el::object hit;
	
				hit["nr"] = nr;
				hit["docNr"] = docNr;
				
				unique_ptr<M6Document> doc(mdb->Fetch(docNr));
				
				hit["id"] = id = doc->GetAttribute("id");
				if (firstId.empty())
					firstId = id;

				hit["title"] = doc->GetAttribute("title");
				
				if (score > 1.f)
					score = 1;
				
				hit["score"] = tr1::trunc(score * 100);

//				vector<string> linked;
//				GetLinkedDbs(db, id, linked);
//				if (not linked.empty())
//				{
//					vector<el::object> links;
//					foreach (string& l, linked)
//						links.push_back(el::object(l));
//					hit["links"] = links;
//				}
				
				hits.push_back(hit);
	
				++nr;
			}
			
			uint32 count = rset->GetCount();
			
			sub.put("hits", el::object(hits));
			sub.put("hitCount", el::object(count));
			sub.put("lastPage", el::object(((count - 1) / hits_per_page) + 1));
			sub.put("last", el::object(nr - 1));
			sub.put("ranked", el::object(ranked));

			hitCount += count;
		}
	}

//	// OK, now if we only have one hit, we might as well show it directly of course...
//	if (hitCount == 1)
//		create_redirect(hitDb, "id", firstId, q, true, request, reply);
//	else
//	{
//		// add some spelling suggestions
//		sort(queryTerms.begin(), queryTerms.end());
//		queryTerms.erase(unique(queryTerms.begin(), queryTerms.end()), queryTerms.end());
//		
//		if (not queryTerms.empty())
//		{
//			vector<el::object> suggestions;
//			
//			foreach (string& term, queryTerms)
//			{
//				try
//				{
//					boost::regex re(string("\\b") + term + "\\b");
//	
//					vector<string> s;
//					SpellCheck(db, term, s);
//					
//					vector<el::object> alternatives;
//					foreach (string& at, s)
//					{
//						el::object alt;
//						alt["term"] = at;
//	
//						// construct new query, with the term replaced by the alternative
//						ostringstream t;
//						ostream_iterator<char, char> oi(t);
//						boost::regex_replace(oi, q.begin(), q.end(), re, at,
//							boost::match_default | boost::format_all);
//	
//						alt["q"] = t.str();
//						alternatives.push_back(alt);
//					}
//					
//					el::object so;
//					so["term"] = term;
//					so["alternatives"] = alternatives;
//					
//					suggestions.push_back(so);
//				}
//				catch (...) {}	// silently ignore errors
//			}
//				
//			if (not suggestions.empty())
//				sub.put("suggestions", el::object(suggestions));
//		}
//
		create_reply_from_template(db == "all" ? "results-for-all.html" : "results.html",
			sub, reply);
//	}
}

// --------------------------------------------------------------------

void M6Server::process_mrs_entry(zeep::xml::element* node, const el::scope& scope, fs::path dir)
{
	
}

void M6Server::process_mrs_link(zeep::xml::element* node, const el::scope& scope, fs::path dir)
{
	string db = node->get_attribute("db");				process_el(scope, db);
	string nr = node->get_attribute("nr");				process_el(scope, nr);
	string id = node->get_attribute("id");				process_el(scope, id);
	string ix = node->get_attribute("index");			process_el(scope, ix);
	string an = node->get_attribute("anchor");			process_el(scope, an);
	string title = node->get_attribute("title");		process_el(scope, title);
	string q = node->get_attribute("q");				process_el(scope, q);

	bool exists = false, entry = false;
	
	if (nr.empty())
	{
		try
		{
			M6Databank* databank = Load(db);
			
			//if (databank != nullptr)
			//{
			//	if (ix.empty())	// create a search link, since we don't know...
			//		exists = mrsDb->DocumentsExist("*", id);
			//	else
			//	{
			//		exists = mrsDb->DocumentsExist(ix, id);
			//		if (exists and ix == "id")
			//			entry = true;
			//	}
			//}
		}
		catch (...) {}
	}
	
	zeep::xml::element* a = new zeep::xml::element("a");
	
	if (not nr.empty())
		a->set_attribute("href",
			(boost::format("entry?db=%1%&nr=%2%%3%%4%")
				% zeep::http::encode_url(db)
				% zeep::http::encode_url(nr)
				% (q.empty() ? "" : ("&q=" + zeep::http::encode_url(q)).c_str())
				% (an.empty() ? "" : (string("#") + zeep::http::encode_url(an)).c_str())
			).str());
	else if (entry)
		a->set_attribute("href",
			(boost::format("entry?db=%1%&id=%2%%3%%4%")
				% zeep::http::encode_url(db)
				% zeep::http::encode_url(id)
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

	zeep::xml::container* parent = node->parent();
	assert(parent);
	parent->insert(node, a);
	
	foreach (zeep::xml::node* c, node->nodes())
	{
		zeep::xml::node* clone = c->clone();
		a->push_back(clone);
		process_xml(clone, scope, dir);
	}
}

void M6Server::process_mrs_redirect(zeep::xml::element* node, const el::scope& scope, fs::path dir)
{
}

// --------------------------------------------------------------------

void M6Server::LoadAllDatabanks()
{
	foreach (zeep::xml::element* db, mConfig->find("dbs/db"))
	{
		string databank = db->content();

		zeep::xml::element* config = M6Config::Instance().LoadConfig(databank);
		if (not config)
		{
			if (VERBOSE)
				cerr << "unknown databank " << databank << endl;
			continue;
		}
		
		zeep::xml::element* file = config->find_first("file");
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
			M6LoadedDatabank db = { new M6Databank(path, eReadOnly), databank };
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
		if (db.mName == inDatabank)
		{
			result = db.mDatabank;
			break;
		}
	}
	
	return result;
}
