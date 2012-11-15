#pragma once

#include <vector>

#include <zeep/http/webapp.hpp>
#include <zeep/http/webapp/el.hpp>
#include <zeep/dispatcher.hpp>

namespace zh = zeep::http;
namespace zx = zeep::xml;
namespace el = zeep::http::el;

class M6Iterator;
class M6Databank;
class M6Parser;
class M6WSSearch;
class M6WSBlast;

typedef std::map<std::string,std::set<M6Databank*>> M6LinkMap;

struct M6AuthInfo;
typedef std::vector<M6AuthInfo*> M6AuthInfoList;

class M6Server : public zh::webapp
{
  public:
					M6Server(zx::element* inConfig);
	virtual			~M6Server();

	virtual void	handle_request(const zh::request& req, zh::reply& rep);
	virtual void	create_unauth_reply(bool stale, const std::string& realm, zh::reply& rep);

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

	std::string		GetEntry(M6Databank* inDatabank, const std::string& inFormat, uint32 inDocNr);
	std::string		GetEntry(M6Databank* inDatabank, const std::string& inFormat,
						const std::string& inIndex, const std::string& inValue);
	
	void			Find(const std::string& inDatabank, const std::string& inQuery,
						bool inAllTermsRequired, uint32 inResultOffset,
						uint32 inMaxResultCount, bool inAddLinks,
						std::vector<el::object>& outHits, uint32& outHitCount, bool& outRanked);

	void			GetLinkedDbs(const std::string& inDb, const std::string& inId, std::vector<std::string>& outLinkedDbs);
	void			AddLinks(const std::string& inDb, const std::string& inId, el::object& inHit);

	uint32			Count(const std::string& inDatabank, const std::string& inQuery);
	std::vector<std::string>
					UnAlias(const std::string& inDatabank);

	const M6DbList&	GetLoadedDatabanks()						{ return mLoadedDatabanks; }

  private:
	virtual void	init_scope(el::scope& scope);

	void			ValidateAuthentication(const zh::request& request, const std::string& inRealm);

	void			handle_download(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_entry(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_file(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_link(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_linked(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_search(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_similar(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_welcome(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_blast(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_blast_submit_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_blast_status_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_blast_results_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_align(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_align_submit_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_status(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_status_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_info(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			handle_admin(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_admin_rename_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply);
	
	void			process_mrs_entry(zx::element* node, const el::scope& scope, boost::filesystem::path dir);
	void			process_mrs_link(zx::element* node, const el::scope& scope, boost::filesystem::path dir);
	void			process_mrs_redirect(zx::element* node, const el::scope& scope, boost::filesystem::path dir);

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

	const zx::element*
					mConfig;
	M6DbList		mLoadedDatabanks;
	M6LinkMap		mLinkMap;

	M6AuthInfoList	mAuthInfo;
	boost::mutex	mAuthMutex;
	std::string		mAdminRealm;
	std::string		mBaseURL;
	bool			mBlastEnabled, mAlignEnabled;
	
	std::vector<zeep::dispatcher*>
					mWebServices;
};
