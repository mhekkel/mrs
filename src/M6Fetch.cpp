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

// --------------------------------------------------------------------
// Regular expression to parse directory listings

#define list_filetype		"([-dlpscbD])"		// 1
#define list_permission		"[-rwxtTsS]"
#define list_statusflags	list_filetype list_permission "{9}"
								// mon = 3, day = 4, year or hour = 5, minutes = 6
#define list_date			"(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +(\\d+) +(\\d+)(?::(\\d+))?"
								// size = 2, name = 7
#define list_line			list_statusflags "\\s+\\d+\\s+\\w+\\s+\\w+\\s+"	\
								"(\\d+)\\s+" list_date " (.+)"

const boost::regex kM6LineParserRE(list_line);

const char* kM6Months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// --------------------------------------------------------------------
// Regular expression to parse URL's

#define url_hexdigit		"[[:digit:]a-fA-F]"
#define url_unreserved		"[-[:alnum:]._~]"
#define url_pct_encoded		"%" url_hexdigit "{2}"
#define url_sub_delims		"[!$&'()*+,;=]"
#define url_userinfo		"(?:((?:" url_unreserved "|" url_pct_encoded "|" url_sub_delims ")+)@)?"
#define url_host			"(\\[(?:[[:digit:]a-fA-F:]+)\\]|(?:" url_unreserved "|" url_pct_encoded "|" url_sub_delims ")+)"
#define url_port			"(?::([[:digit:]]+))?"
#define url_pchar			url_unreserved "|" url_pct_encoded "|" url_sub_delims "|:|@"
#define url_path			"(?:/((?:" url_pchar "|\\*|\\?|/)*))?"
	
const boost::regex kM6URLParserRE("ftp://" url_userinfo url_host url_port url_path);

// --------------------------------------------------------------------

class M6FTPFetcher
{
  public:
						M6FTPFetcher(zx::element* inConfig);
						~M6FTPFetcher();
	
	void				Mirror();

  private:

	void				Login();

	void				CollectFiles(fs::path inLocalDir,
							fs::path::iterator p, fs::path::iterator e);
	
	uint32				WaitForReply(); 
	uint32				SendAndWaitForReply(const string& inCommand, const string& inParam); 
	
	void				ListFiles(const string& inPattern,
							boost::function<void(char inType, const string&, size_t, time_t)> inProc);
	
	void				Retrieve();

	void				Error(const char* inError);

	boost::asio::io_service
						mIOService;
	at::tcp::resolver	mResolver;
	at::tcp::socket		mSocket;

	string				mServer, mUser, mPassword, mPort;
	string				mReply;
	fs::path			mPath, mDstDir;

	struct FileToFetch
	{
		fs::path		local, remote;
		size_t			size;
		time_t			time;
	};

	vector<FileToFetch>	mFilesToFetch;
};

M6FTPFetcher::M6FTPFetcher(zx::element* inConfig)
	: mResolver(mIOService), mSocket(mIOService)
{
	zx::element* fetch = inConfig->find_first("fetch");

	string src = fetch->get_attribute("src");
	boost::smatch m;
	if (not boost::regex_match(src, m, kM6URLParserRE))
		THROW(("Invalid source url: <%s>", src.c_str()));
	
	mServer = m[2];
	mUser = m[1];
	if (mUser.empty())
		mUser = "anonymous";
	mPort = m[3];
	if (mPort.empty())
		mPort = "ftp";
	mPath = fs::path(m[4].str());

    string dst = fetch->get_attribute("dst");
    if (dst.empty())
        dst = inConfig->get_attribute("id");

    fs::path rawdir = M6Config::Instance().FindGlobal("/m6-config/rawdir");
    mDstDir = rawdir / dst;

    if (fs::exists(mDstDir) and not fs::is_directory(mDstDir))
        THROW(("Destination for fetch should be a directory"));
}

M6FTPFetcher::~M6FTPFetcher()
{
}

void M6FTPFetcher::Error(const char* inError)
{
	THROW(("%s: %s\n(trying to access server %s)",
		inError, mReply.c_str(), mServer.c_str()));
}

void M6FTPFetcher::Login()
{
	uint32 status = SendAndWaitForReply("user", mUser);
	if (status == 331)
		status = SendAndWaitForReply("pass", mPassword);
	if (status != 230 and status != 202)
		Error("Error logging in");
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

	uint32 status = WaitForReply();
	if (status != 220)
		Error("Failed to connect");

	Login();

	CollectFiles(fs::path(), mPath.begin(), mPath.end());
}

uint32 M6FTPFetcher::SendAndWaitForReply(const string& inCommand, const string& inParam)
{
	boost::asio::streambuf request;
	ostream request_stream(&request);
	request_stream << inCommand << ' ' << inParam << "\r\n";
	boost::asio::write(mSocket, request);

	return WaitForReply();
}

uint32 M6FTPFetcher::WaitForReply()
{
	boost::asio::streambuf response;
	boost::asio::read_until(mSocket, response, "\r\n");

	istream response_stream(&response);

	string line;
	getline(response_stream, line);

	if (line.length() < 3 or not isdigit(line[0]) or not isdigit(line[1]) or not isdigit(line[2]))
		THROW(("FTP Server returned unexpected line:\n\"%s\"", line.c_str()));

	uint32 result = ((line[0] - '0') * 100) + ((line[1] - '0') * 10) + (line[2] - '0');
	mReply = line;

	if (line.length() >= 4 and line[3] == '-')
	{
		do
		{
			boost::asio::read_until(mSocket, response, "\r\n");
			istream response_stream(&response);
			getline(response_stream, line);
			mReply = mReply + '\n' + line;
		}
		while (line.length() < 3 or line.substr(0, 3) != mReply.substr(0, 3));
	}

	return result;
}

void M6FTPFetcher::CollectFiles(fs::path inLocalDir, fs::path inRemoteDir, fs::path::iterator p, fs::path::iterator e)
{
	string s = p->string();
	bool isPattern = ba::contains(s, "*") or ba::contains(s, "?");
	++p;

	vector<string> dirs;

	if (p == e)
	{
		fs::path localDir = mDstDir / inLocalDir;

			// we've reached the end of the url
		if (not fs::exists(localDir))
			fs::create_directories(localDir);

		// If the file part is a pattern we need to list and perhaps to clean up
		if (isPattern)
		{
			vector<fs::path> existing;

			fs::directory_iterator end;
			for (fs::directory_iterator file(localDir); file != end; ++file)
			{
				if (fs::is_regular_file(*file))
					existing.push_back(*file);
			}

			ListFiles(s, [this, &existing, &localDir](char inType, const string& inFile, size_t inSize, time_t inTime)
			{
				fs::path file = localDir / inFile;

//				if (inType == 'l')
//				{
//					file.filename() = 
//				}
//				else if (inType != '-')
//					continue;

				if (fs::exists(file))
				{
					existing.erase(find(existing.begin(), existing.end(), file), existing.end());

					if (fs::last_write_time(file) >= inTime)
					{
						if (VERBOSE)
							cerr << file << " is up-to-date" << endl;
						continue;
					}
				}

				FileToFetch need = { file, inRemoteDir / inFile, inSize, inTime };
				this->mFilesToFetch.push_back(need);
			});
			
			foreach (fs::path file, needed)
			{
				cerr << "Need to fetch " << file << endl;
			}
			
			foreach (fs::path file, existing)
				cerr << "Need to delete " << file << endl;
		}
	}
	else if (isPattern)
	{
		ListFiles(s, [&dirs, &s](char inType, const string& inFile, size_t inSize, time_t inTime)
		{
			if (inType == 'd')
				dirs.push_back(inFile);
		});
	}
	else
		dirs.push_back(s);

	foreach (const string& dir, dirs)
	{
		uint32 status = SendAndWaitForReply("cwd", dir);
		if (status != 250)
			Error((string("Error changing directory to ") + dir).c_str());
		
		fs::path localDir(inLocalDir);
		if (not inLocalDir.empty())
			localDir /= dir;
		else if (isPattern)
			localDir = dir;
		
		CollectFiles(localDir, p, e);
		status = SendAndWaitForReply("cdup", "");
		if (status != 200 and status != 250)
			Error("Error changing directory");
	}
}

void M6FTPFetcher::ListFiles(const string& inPattern,
	boost::function<void(char, const string&, size_t, time_t)> inProc)
{
	uint32 status = SendAndWaitForReply("pasv", "");
	if (status != 227)
		Error("Passive mode failed");
	
	boost::regex re("227 Entering Passive Mode \\((\\d+,\\d+,\\d+,\\d+),(\\d+),(\\d+)\\)\\s*");
	boost::smatch m;
	if (not boost::regex_match(mReply, m, re))
		Error("Invalid reply for passive command");
	
	string address = m[1];
	ba::replace_all(address, ",", ".");
	
	string port = boost::lexical_cast<string>(
		boost::lexical_cast<uint32>(m[2]) * 256 +
		boost::lexical_cast<uint32>(m[3]));

	at::tcp::iostream stream;
//	stream.expires_from_now(boost::posix_time::seconds(60));
	stream.connect(address, port);
		
	// Yeah, we have a data connection, now send the List command
	status = SendAndWaitForReply("list", "");


	time_t now;
	gmtime(&now);
	
	for (;;)
	{
		string line;
		getline(stream, line);
		if (line.empty() and stream.eof())
			break;
		
		ba::trim(line);
		
		if (boost::regex_match(line, m, kM6LineParserRE))
		{
			struct tm t = {}, n;
#ifdef _MSC_VER
			gmtime_s(&n, &now);
#else
			gmtime_r(&now, &n);
#endif
			
			t.tm_mday = boost::lexical_cast<int>(m[4]);
			for (t.tm_mon = 0; t.tm_mon < 12; ++t.tm_mon)
			{
				if (ba::iequals(kM6Months[t.tm_mon], m[3].str()))
					break;
			}

			if (m[6].str().empty())
				t.tm_year = boost::lexical_cast<int>(m[5]) - 1900;
			else
			{
				t.tm_year = n.tm_year;
				t.tm_hour = boost::lexical_cast<int>(m[5]);
				t.tm_min = boost::lexical_cast<int>(m[6]);
			}
			
			string file = m[7];
			size_t size = boost::lexical_cast<size_t>(m[2]);
			time_t time = mktime(&t);
			
			if (inPattern.empty() or M6FilePathNameMatches(file, inPattern))
				inProc(line[0], file, size, time);
		}
	}
	
	if (status < 200)
		status = WaitForReply();
	if (status != 226)
		Error("Error listing");
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
