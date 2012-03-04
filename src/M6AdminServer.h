#pragma once

#include <vector>

#include <zeep/http/webapp.hpp>
#include <zeep/http/webapp/el.hpp>

namespace zh = zeep::http;
namespace zx = zeep::xml;
namespace el = zeep::http::el;

class M6Databank;
struct M6AuthInfo;
typedef std::vector<M6AuthInfo*> M6AuthInfoList;

class M6AdminServer : public zeep::http::webapp
{
  public:
					M6AdminServer(zx::element* inConfig);

	virtual void	handle_request(const zh::request& req, zh::reply& rep);

  private:

	virtual void	init_scope(el::scope& scope);

	void			handle_welcome(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_file(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			ValidateAuthentication(const std::string& inURI, const std::string& inAuthentication);

	zx::element*	mConfig;
	M6AuthInfoList	mAuthInfo;
};
