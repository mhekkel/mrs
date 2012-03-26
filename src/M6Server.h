#pragma once

#include <vector>

#include <zeep/http/webapp.hpp>
#include <zeep/http/webapp/el.hpp>

namespace zh = zeep::http;
namespace zx = zeep::xml;
namespace el = zeep::http::el;

class M6Iterator;
class M6Databank;

struct M6AuthInfo;
typedef std::vector<M6AuthInfo*> M6AuthInfoList;

class M6Server : public zh::webapp
{
  public:
					M6Server(zx::element* inConfig);

	virtual void	handle_request(const zh::request& req, zh::reply& rep);
	virtual void	create_unauth_reply(bool stale, zh::reply& rep);

  private:
	virtual void	init_scope(el::scope& scope);

	void			ValidateAuthentication(const zh::request& request);

	void			handle_entry(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_file(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_search(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_welcome(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_admin(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			process_mrs_entry(zx::element* node, const el::scope& scope, boost::filesystem::path dir);
	void			process_mrs_link(zx::element* node, const el::scope& scope, boost::filesystem::path dir);
	void			process_mrs_redirect(zx::element* node, const el::scope& scope, boost::filesystem::path dir);

	void			create_redirect(const std::string& databank, uint32 inDocNr,
						const std::string& q, bool redirectForQuery,
						const zh::request& req, zh::reply& rep);
	void			highlight_query_terms(zx::element* node, boost::regex& expr);

	void			LoadAllDatabanks();
	M6Databank*		Load(const std::string& inDatabank);
	
	void			SpellCheck(const std::string& inDatabank, const std::string& inTerm,
						std::vector<std::pair<std::string,uint16>>& outCorrections);

	struct M6LoadedDatabank
	{
		M6Databank*	mDatabank;
		std::string	mID, mName;
	};
	typedef std::vector<M6LoadedDatabank> M6DbList;

	void			Find(M6Databank* inDatabank,
						const std::string& inQuery, bool inAllTermsRequired,
						uint32 inResultOffset, uint32 inMaxResultCount,
						std::vector<el::object>& outHits, uint32& outHitCount, bool& outRanked);

	uint32			Count(const std::string& inDatabank, const std::string& inQuery);

	zx::element*	mConfig;
	M6DbList		mLoadedDatabanks;
	M6AuthInfoList	mAuthInfo;
	boost::mutex	mAuthMutex;
};
