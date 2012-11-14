#pragma once

#include <boost/filesystem/path.hpp>
#include <zeep/xml/document.hpp>

class M6Config
{
  public:

	static void			SetConfigFile(
							const boost::filesystem::path& inConfigFile);
	static M6Config&	Instance();

	std::string			FindGlobal(const std::string& inXPath);
	zeep::xml::element_set
						Find(const std::string& inXPath);
	zeep::xml::element*	FindFirst(const std::string& inXPath);

	zeep::xml::element*	LoadDatabank(const std::string& inDatabank);
	zeep::xml::element_set
						LoadDatabanks();

	void				ExpandDatabankAlias(const std::string& inAlias,
							std::vector<std::string>& outDatabanks);

	zeep::xml::element*	LoadParser(const std::string& inParser);

	zeep::xml::element_set
						LoadServers();

	zeep::xml::element*	LoadFormat(const std::string& inDatabank);
	std::string			LoadFormatScript(const std::string& inDatabank);
	
	void				SetPassword(const std::string& inRealm,
							const std::string& inUser, const std::string& inPassword);

  private:
						M6Config();
						~M6Config();

	void				WriteOut();

	zeep::xml::document*			mConfig;
	static boost::filesystem::path	sConfigFile;
};

