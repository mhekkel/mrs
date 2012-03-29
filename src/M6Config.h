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

	zeep::xml::element*	LoadDatabank(const std::string& inDatabank);
	zeep::xml::element_set
						LoadDatabanks();

	zeep::xml::element*	LoadParser(const std::string& inParser);

	zeep::xml::element_set
						LoadServers();

	zeep::xml::element*	LoadFormat(const std::string& inDatabank);
	std::string			LoadFormatScript(const std::string& inDatabank);

  private:
						M6Config();
						~M6Config();

	zeep::xml::document*				mConfig;
	static boost::filesystem::path		sConfigFile;
};

