#pragma once

#include <vector>

#include <zeep/http/webapp.hpp>
#include <zeep/http/webapp/el.hpp>

namespace zh = zeep::http;
namespace el = zeep::http::el;

class M6Iterator;
class M6Databank;

class M6Server : public zeep::http::webapp
{
  public:
					M6Server(zeep::xml::element* inConfig);

	virtual void	handle_request(const zh::request& req, zh::reply& rep);

  private:
	virtual void	init_scope(el::scope& scope);

	void			handle_entry(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_file(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_search(const zh::request& request, const el::scope& scope, zh::reply& reply);
	void			handle_welcome(const zh::request& request, const el::scope& scope, zh::reply& reply);

	void			process_mrs_entry(zeep::xml::element* node, const el::scope& scope, boost::filesystem::path dir);
	void			process_mrs_link(zeep::xml::element* node, const el::scope& scope, boost::filesystem::path dir);
	void			process_mrs_redirect(zeep::xml::element* node, const el::scope& scope, boost::filesystem::path dir);

	void			LoadAllDatabanks();
	M6Databank*		Load(const std::string& inDatabank);
	
	struct M6LoadedDatabank
	{
		M6Databank*	mDatabank;
		std::string	mName;
	};
	typedef std::vector<M6LoadedDatabank> M6DbList;

	zeep::xml::element*
					mConfig;
	M6DbList		mLoadedDatabanks;
};
