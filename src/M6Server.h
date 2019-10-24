//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//	 (See accompanying file LICENSE_1_0.txt or copy at
//		   http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <vector>
#include <map>
#include <set>

#include <tuple>

#include <boost/thread/mutex.hpp>

#include <zeep/http/webapp.hpp>
#include <zeep/el/element.hpp>
#include <zeep/dispatcher.hpp>

#include "M6Config.h"

namespace zh = zeep::http;
namespace zx = zeep::xml;
namespace el = zeep::el;

class M6Iterator;
class M6Databank;
class M6Parser;
class M6WSSearch;
class M6WSBlast;

typedef std::map<std::string,std::set<M6Databank*>> M6LinkMap;

class M6Server : public zh::webapp
{
  public:
					M6Server(const zx::element* inConfig);
	virtual			~M6Server();

	static M6Server*
					Instance()					{ return sInstance; }

	static int		Start(const std::string& inRunAs, const std::string& inPidFile,
						bool inForeground);
	static int		Stop(const std::string& inPidFile);
	static int		Status(const std::string& inPidFile);
	static int		Reload(const std::string& inPidFile);

	virtual void	handle_request(const zh::request& req, zh::reply& rep);

	void			LoadAllDatabanks();
	M6Databank*		Load(const std::string& inDatabank);

	struct M6LoadedDatabank
	{
		M6Databank*				mDatabank;
		std::string				mID, mName;
		std::set<std::string>	mAliases;
		bool					mBlast;
		M6Parser*				mParser;
	};
	typedef std::vector<M6LoadedDatabank> M6DbList;
	
	struct M6BlastDatabank
	{
		std::string				mID, mName;
		std::set<std::string>	mIDs;
	};
	typedef std::vector<M6BlastDatabank> M6BlastDbList;

	std::tuple<M6Databank*,uint32>
					GetEntryDatabankAndNr(const std::string& inDatabank, const std::string& inID);
	std::string		GetEntry(M6Databank* inDatabank, const std::string& inFormat, uint32 inDocNr);
	std::string		GetEntry(M6Databank* inDatabank, const std::string& inFormat,
						const std::string& inIndex, const std::string& inValue);
	std::string		GetEntry(const std::string& inDB, const std::string& inID,
						const std::string& inFormat);

	void			Find(const std::string& inDatabank, const std::string& inQuery,
						bool inAllTermsRequired, uint32 inResultOffset,
						uint32 inMaxResultCount, bool inAddLinks,
						std::vector<el::object>& outHits, uint32& outHitCount, bool& outRanked,
						std::string& outParseError);

	void			GetLinkedDbs(const std::string& inDb, const std::string& inId, std::vector<std::string>& outLinkedDbs);
	void			AddLinks(const std::string& inDb, const std::string& inId, el::object& inHit);

	uint32			Count(const std::string& inDatabank, const std::string& inQuery);
	std::vector<std::string>
					UnAlias(const std::string& inDatabank);
	std::vector<std::string>
					GetAliases(const std::string& inDatabank);

	const M6DbList&	GetLoadedDatabanks()						{ return mLoadedDatabanks; }

  private:
					M6Server(const M6Server&);
	M6Server&		operator=(const M6Server&);
	  
	virtual void	init_scope(el::scope& scope);

	virtual std::string
					get_hashed_password(const std::string& username, const std::string& realm);
	void			ProcessNewConfig(const std::string& inPage, const zh::request& request);

	void			handle_download(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_entry(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_file(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_link(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_linked(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_search(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_similar(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_welcome(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_search_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_rest(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_rest_entry(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_rest_find(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_blast(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_blast_submit_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_blast_status_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_blast_results_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_align(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_align_submit_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_status(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_status_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_info(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_browse(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_admin(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_admin_blast_queue_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_admin_blast_delete_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	
	void			process_mrs_link(zx::element* node, const el::scope& scope, boost::filesystem::path dir);
	void			process_mrs_enable(zx::element* node, const el::scope& scope, boost::filesystem::path dir);

	void			create_redirect(const std::string& databank, const std::string& inIndex, const std::string& inValue,
						const std::string& q, bool redirectForQuery, const zh::request& req, zh::reply& rep);

	void			create_redirect(const std::string& databank, uint32 inDocNr,
						const std::string& q, bool redirectForQuery,
						const zh::request& req, zh::reply& rep);

	void			highlight_query_terms(zx::element* node, boost::regex& expr);
	void			create_link_tags(zx::element* node, boost::regex& expr, const std::string& inDatabank,
						const std::string& inIndex, const std::string& inID, const std::string& inAnchor);

	void			SpellCheck(const std::string& inDatabank, const std::string& inTerm,
						std::vector<std::pair<std::string,uint16>>& outCorrections);

	static M6Server*
					sInstance;

	const zx::element*
					mConfig;
	M6DbList		mLoadedDatabanks;
	M6BlastDbList	mBlastDatabanks;
	M6LinkMap		mLinkMap;

	boost::mutex	mAuthMutex;
	std::string		mBaseURL;
	bool			mAlignEnabled;

	M6Config::File*	mConfigCopy;
	
	std::vector<zeep::dispatcher*>
					mWebServices;
};
