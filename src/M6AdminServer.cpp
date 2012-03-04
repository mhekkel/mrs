#include "M6Lib.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/tr1/cmath.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/random/random_device.hpp>

#include "M6Databank.h"
#include "M6AdminServer.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"
#include "M6Config.h"
#include "M6MD5.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

const string kM6AdminServerNS = "http://mrs.cmbi.ru.nl/mrs-web/ml";

// --------------------------------------------------------------------

M6AdminServer::M6AdminServer(zeep::xml::element* inConfig)
	: webapp(kM6AdminServerNS)
	, mConfig(inConfig)
{
	string docroot = "docroot";
	zeep::xml::element* e = mConfig->find_first("docroot");
	if (e != nullptr)
		docroot = e->content();
	set_docroot(docroot);
	
	mount("",				boost::bind(&M6AdminServer::handle_welcome, this, _1, _2, _3));
	mount("scripts",		boost::bind(&M6AdminServer::handle_file, this, _1, _2, _3));
	mount("css",			boost::bind(&M6AdminServer::handle_file, this, _1, _2, _3));
	mount("images",			boost::bind(&M6AdminServer::handle_file, this, _1, _2, _3));
	mount("favicon.ico",	boost::bind(&M6AdminServer::handle_file, this, _1, _2, _3));
}

void M6AdminServer::init_scope(el::scope& scope)
{
	webapp::init_scope(scope);
}

// --------------------------------------------------------------------

struct not_authorized : public std::exception
{
		not_authorized(bool inStale = false) : mStale(inStale) {}

	bool mStale;
};

void M6AdminServer::handle_request(const zh::request& req, zh::reply& rep)
{
	string authorization, host;
	foreach (const zeep::http::header& h, req.headers)
	{
		if (ba::iequals(h.name, "Authorization"))
			authorization = h.value;
		if (ba::iequals(h.name, "Host"))
			host = h.value;
	}

	try
	{
		if (authorization.empty())
			throw not_authorized(false);

		ValidateAuthentication(req.uri, authorization);
		
		zh::webapp::handle_request(req, rep);
	}
	catch (not_authorized& na)
	{
		rep = zh::reply::stock_reply(zh::unauthorized);
		
		string realm = "admin@" + host;
		
		boost::random::random_device rng;
		uint32 data[4] = { rng(), rng(), rng(), rng() };
		M6MD5 nonceHash;
		nonceHash.Update(data, sizeof(data));
		string nonce = nonceHash.Finalise();
		
		bool stale = na.mStale;

		string challenge = "Digest ";
		challenge += "realm=\"" + realm + "\", qop=\"auth\", nonce=\"" + nonce + '"';

		rep.set_header("WWW-Authenticate", challenge); 
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

void M6AdminServer::ValidateAuthentication(const string& inURI, const string& inAuthentication)
{
	map<string,string> info;
	boost::regex re("(?|(username|realm|nonce|uri|qop|cnonce|response)=\"([^\"]+)\"|(nc)=(\\d+)|(qop)=(\\w+))");
	const char* b = inAuthentication.c_str();
	const char* e = b + inAuthentication.length();
	boost::match_results<const char*> m;
	while (b < e and boost::regex_search(b, e, m, re))
	{
		info[string(m[1].first, m[1].second)] = string(m[2].first, m[2].second);
		b = m[0].second;
	}
	
	string user = info["username"];
	string realm = info["realm"];
	string nonce = info["nonce"];
	string nc = info["nc"];
	string cnonce = info["cnonce"];
	string qop = info["qop"];
	
	string password = "tiger";
	
	M6MD5 ha1(user + ':' + realm + ':' + password);
	M6MD5 ha2("GET:");
	ha2.Update(inURI.c_str(), inURI.length());
	M6MD5 h(
		ha1.Finalise() + ':' +
		nonce + ':' +
		nc + ':' +
		cnonce + ':' +
		qop + ':' +
		ha2.Finalise());
	
	if (info["response"] != h.Finalise())
		throw not_authorized(true);
}

// --------------------------------------------------------------------

void M6AdminServer::handle_welcome(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	create_reply_from_template("index.html", scope, reply);
}

void M6AdminServer::handle_file(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	fs::path file = get_docroot() / scope["baseuri"].as<string>();
	
	webapp::handle_file(request, scope, reply);
	
	if (file.extension() == ".html")
		reply.set_content_type("application/xhtml+xml");
}

