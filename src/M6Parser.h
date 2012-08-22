#pragma once

#include <boost/thread/tss.hpp>

#include "M6Document.h"

class M6Parser
{
  public:
	
	virtual			~M6Parser() {}
	
	virtual void	ParseDocument(M6InputDocument* inDoc);

  protected:
	
	void			SetAttribute(const std::string& inName,
						const char* inText, size_t inSize)
					{
						mDoc->SetAttribute(inName, inText, inSize);
					}

	void			Index(const std::string& inIndex,
						M6DataType inDataType, bool isUnique,
						const char* inText, size_t inSize)
					{
						mDoc->Index(inIndex, inDataType, isUnique, inText, inSize);
					}

  private:
	M6InputDocument* mDoc;
};

//class M6XMLScriptParser : public M6Parser
//{
//  public:
//					M6XMLScriptParser(zeep::xml::element* inScript);
//	virtual void	ParseDocument(M6InputDocument* inDoc);
//};

struct M6PerlParserImpl;
typedef boost::thread_specific_ptr<M6PerlParserImpl>	M6PerlParserImplPtr;

class M6PerlParser : public M6Parser
{
  public:
					M6PerlParser(const std::string& inName);

	virtual void	ParseDocument(M6InputDocument* inDoc);

  private:
	std::string			mName;
	M6PerlParserImplPtr	mImpl;
};
