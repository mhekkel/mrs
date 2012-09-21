#include "M6Lib.h"

#include <time.h>
#include <iostream>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/regex.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>

#include "M6Config.h"
#include "M6Error.h"
#include "M6Progress.h"
#include "M6File.h"

using namespace std;
namespace zx = zeep::xml;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace at = boost::asio::ip;

class M6FTPFetcher
{
  public:
						M6FTPFetcher(zx::element* inConfig);
						~M6FTPFetcher();
	
	void				Mirror();

  private:

	void				CollectFiles(fs::path::iterator p, fs::path::iterator e);
	
	void				WaitForResponse(uint32& outStatus, string& outText); 
	void				SendAndWaitForResponse(const string& inCommand, const string& inParam,
							uint32& outStatus, string& outText); 
	void				SendAndWaitForStatus(const string& inCommand, const string& inParam,
							uint32 inExpectedStatus);
	uint32				SendAndWait(const string& inCommand, const string& inParam); 
	
	void				ListFiles(const string& inPattern,
							boost::function<void(const string&, size_t, time_t)> inProc);
	
	void				Retrieve();


	boost::asio::io_service
						mIOService;
	at::tcp::resolver	mResolver;
	at::tcp::socket		mSocket;

	string				mServer, mUser, mPassword, mPort;
	string				mReply;
	fs::path			mPath;
	vector<fs::path>	mFilesToFetch;
};

M6FTPFetcher::M6FTPFetcher(zx::element* inConfig)
	: mResolver(mIOService), mSocket(mIOService)
{
	zx::element* fetch = inConfig->find_first("fetch");

#define url_hexdigit		"[[:digit:]a-fA-F]"
#define url_unreserved		"[-[:alnum:]._~]"
#define url_pct_encoded		"%" url_hexdigit "{2}"
#define url_sub_delims		"[!$&'()*+,;=]"
#define url_userinfo		"(?:((?:" url_unreserved "|" url_pct_encoded "|" url_sub_delims ")+)@)?"
#define url_host			"(\\[(?:[[:digit:]a-fA-F:]+)\\]|(?:" url_unreserved "|" url_pct_encoded "|" url_sub_delims ")+)"
#define url_port			"(?::([[:digit:]]+))?"
#define url_pchar			url_unreserved "|" url_pct_encoded "|" url_sub_delims "|:|@"
#define url_path			"(?:/((?:" url_pchar "|\\*|\\?|/)*))?"
	
	boost::regex re("ftp://" url_userinfo url_host url_port url_path);

	string src = fetch->get_attribute("src");
	boost::smatch m;
	if (not boost::regex_match(src, m, re))
		THROW(("Invalid source url: <%s>", src.c_str()));
	
	mServer = m[2];
	mUser = m[1];
	if (mUser.empty())
		mUser = "anonymous";
	mPort = m[3];
	if (mPort.empty())
		mPort = "ftp";
	mPath = fs::path(m[4].str());
}

M6FTPFetcher::~M6FTPFetcher()
{
}

void M6FTPFetcher::Mirror()
{
	// Get a list of endpoints corresponding to the server name.
	at::tcp::resolver::query query(mServer, mPort);
	at::tcp::resolver::iterator endpoint_iterator = mResolver.resolve(query);
	at::tcp::resolver::iterator end;

	// Try each endpoint until we successfully establish a connection.
	boost::system::error_code error = boost::asio::error::host_not_found;
    while (error && endpoint_iterator != end)
    {
		mSocket.close();
		mSocket.connect(*endpoint_iterator++, error);
	}

	if (error)
		throw boost::system::system_error(error);

	string text;
	uint32 status;
	
	WaitForResponse(status, text);
	if (status != 220)
		THROW(("Failed to connect: %s", text));

	SendAndWaitForResponse("user", mUser, status, text);
	if (status == 331)
		SendAndWaitForResponse("pass", mPassword, status, text);
	if (status != 230)
		THROW(("Failed to log in: %s", text));

	CollectFiles(mPath.begin(), mPath.end());
}

void M6FTPFetcher::SendAndWaitForResponse(const string& inCommand, const string& inParam, uint32& outStatus, string& outText)
{
	boost::asio::streambuf request;
	ostream request_stream(&request);
	request_stream << inCommand << ' ' << inParam << "\r\n";
	boost::asio::write(mSocket, request);

	WaitForResponse(outStatus, outText);
}

void M6FTPFetcher::SendAndWaitForStatus(
	const string& inCommand, const string& inParam, uint32 inExpectedStatus)
{
	uint32 status;
	string text;
	
	SendAndWaitForResponse(inCommand, inParam, status, text);
	if (status != inExpectedStatus)
		THROW(("Failed to execute cmd %s, result was: %s", inCommand.c_str(), text.c_str()));
}

uint32 M6FTPFetcher::SendAndWait(const string& inCommand, const string& inParam)
{
	uint32 status;
	SendAndWaitForResponse(inCommand, inParam, status, mReply);
	return status;
}

void M6FTPFetcher::WaitForResponse(uint32& outStatus, string& outText)
{
	boost::asio::streambuf response;
	boost::asio::read_until(mSocket, response, "\r\n");

	istream response_stream(&response);

	string line;
	getline(response_stream, line);

	if (line.length() < 3 or not isdigit(line[0]) or not isdigit(line[1]) or not isdigit(line[2]))
		THROW(("FTP Server returned unexpected line:\n\"%s\"", line.c_str()));

	outStatus = ((line[0] - '0') * 100) + ((line[1] - '0') * 10) + (line[2] - '0');
	outText = line;

	if (line.length() >= 4 and line[3] == '-')
	{
		do
		{
			boost::asio::read_until(mSocket, response, "\r\n");
			istream response_stream(&response);
			getline(response_stream, line);
			outText = outText + '\n' + line;
		}
		while (line.length() < 3 or line.substr(0, 3) != outText.substr(0, 3));
	}
}

void M6FTPFetcher::CollectFiles(fs::path::iterator p, fs::path::iterator end)
{
	string s = p->string();
	bool isPattern = ba::contains(s, "*") or ba::contains(s, "?");
	++p;
	
	if (p == end)
	{
		// we've reached the end of the url
		// If the file part is a pattern we need to list
		if (isPattern)
		{
			ListFiles(s, [](const string& inFile, size_t inSize, time_t inTime)
			{
				cerr << inFile << endl;
			});
		}
		
	}
	else if (isPattern)
	{
		THROW(("Unimplemented"));
	}
	else
	{
		uint32 status;
		string text;

		SendAndWaitForResponse("cwd", s, status, text);

		if (status == 250)
			CollectFiles(p, end);
	}
}

void M6FTPFetcher::ListFiles(const string& inPattern,
	boost::function<void(const string&, size_t, time_t)> inProc)
{
	uint32 status = SendAndWait("pasv", "");
	if (status != 227)
		THROW(("Passive mode failed: %s", mReply.c_str()));
	
	boost::regex re("227 Entering Passive Mode \\((\\d+,\\d+,\\d+,\\d+),(\\d+),(\\d+)\\)\\s*");
	boost::smatch m;
	if (not boost::regex_match(mReply, m, re))
		THROW(("Invalid reply for passive command: %s", mReply.c_str()));
	
	string address = m[1];
	ba::replace_all(address, ",", ".");
	
	string port = boost::lexical_cast<string>(
		boost::lexical_cast<uint32>(m[2]) * 256 +
		boost::lexical_cast<uint32>(m[3]));

	at::tcp::iostream stream;
	stream.expires_from_now(boost::posix_time::seconds(60));
	stream.connect(address, port);
		
	// Yeah, we have a data connection, now send the List command
	status = SendAndWait("list", "");

#define list_filetype		"([-dlpscbD])"		// 1
#define list_permission		"[-rwxtTsS]"
#define list_statusflags	list_filetype list_permission "{9}"
								// mon = 3, day = 4, year or hour = 5, minutes = 6
#define list_date			"(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +(\\d+) +(\\d+)(?::(\\d+))?"
								// size = 2, name = 7
#define list_line			list_statusflags "\\s+\\d+\\s+\\w+\\s+\\w+\\s+"	\
								"(\\d+)\\s+" list_date " (.+)"

	boost::regex re2(list_line);

	time_t now;
	gmtime(&now);
	
	for (;;)
	{
		string line;
		getline(stream, line);
		if (line.empty() and stream.eof())
			break;
		
		ba::trim(line);
		
		if (boost::regex_match(line, m, re2))
		{
			struct tm t;
#ifdef _MSC_VER
			gmtime_s(&t, &now);
#else
			gmtime_r(&now, &t);
#endif

//				string time;
//				if (m[4].str().empty())
//					time = (boost::format("%2.2d %3.3s %4.4d") % m[2] % m[1] % m[3]).str();
//				else
//					time = (boost::format("%2.2d %3.3s %4.4d %2.2d:%2.2d") % m[2] % m[1] % (1900 + t.tm_year) % m[3] % m[4]).str();
//
//				result = curl_getdate(time.c_str(), &now);
//			}

			size_t size = boost::lexical_cast<size_t>(m[2]);
			string file = m[7];
			
			if (inPattern.empty() or M6FilePathNameMatches(file, inPattern))
				inProc(file, size, 0);
		}
	}
}


void M6Fetch(const string& inDatabank)
{
	zx::element* config = M6Config::Instance().LoadDatabank(inDatabank);
	if (not config)
		THROW(("Configuration for %s is missing", inDatabank.c_str()));

//	M6Curl::Instance().Mirror(config);
	M6FTPFetcher fetch(config);
	fetch.Mirror();
}
