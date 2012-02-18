#pragma once

#include <boost/filesystem/path.hpp>
#include <zeep/xml/document.hpp>

class M6Config
{
  public:

	static void			SetConfigFile(
							const boost::filesystem::path& inConfigFile);
	static M6Config&	Instance();

	zeep::xml::element*	LoadConfig(const std::string& inDatabank);
	zeep::xml::element*	LoadParser(const std::string& inParser);

  private:
						M6Config();
						~M6Config();

	zeep::xml::document*				mConfig;
	static boost::filesystem::path		sConfigFile;
};

