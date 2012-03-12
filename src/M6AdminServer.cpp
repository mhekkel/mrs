#include "M6Lib.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/tr1/cmath.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/random/random_device.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

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
namespace pt = boost::posix_time;

const string kM6AdminServerNS = "http://mrs.cmbi.ru.nl/mrs-web/ml";

// --------------------------------------------------------------------

struct not_authorized : public std::exception
{
		not_authorized(bool inStale = false) : mStale(inStale) {}

	bool mStale;
};

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
	return age.total_seconds() > 5;
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
	}
	return valid;
}

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

void M6AdminServer::handle_request(const zh::request& req, zh::reply& rep)
{
	string authorization;
	foreach (const zeep::http::header& h, req.headers)
	{
		if (ba::iequals(h.name, "Authorization"))
			authorization = h.value;
	}

	try
	{
		if (authorization.empty())
			throw not_authorized(false);

		ValidateAuthentication(req.method, req.uri, authorization);
		
		zh::webapp::handle_request(req, rep);
	}
	catch (not_authorized& na)
	{
		boost::mutex::scoped_lock lock(mAuthMutex);
		
		rep = zh::reply::stock_reply(zh::unauthorized);
		
		if (zx::element* realm = mConfig->find_first("realm"))
			mAuthInfo.push_back(new M6AuthInfo(realm->get_attribute("name")));
		else
			THROW(("Realm missing from config file"));
		
		string challenge = mAuthInfo.back()->GetChallenge();
		
		if (na.mStale)
			challenge += ", stale=\"true\"";

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

void M6AdminServer::ValidateAuthentication(
	const string& inMethod, const string& inURI, const string& inAuthentication)
{
	map<string,string> info;
	
	boost::regex re("(\\w+)=(?|\"([^\"]*)\"|'([^']*)'|(\\w+))(?:,\\s*)?");
	const char* b = inAuthentication.c_str();
	const char* e = b + inAuthentication.length();
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
				and auth->Validate(inMethod, inURI, ha1, info))
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
		throw not_authorized(stale);
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

