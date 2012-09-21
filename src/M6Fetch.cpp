#include "M6Lib.h"

#include <time.h>
#include <iostream>

#include "curl/curl.h"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/regex.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Config.h"
#include "M6Error.h"
#include "M6Progress.h"
#include "M6File.h"

#pragma comment ( lib, "libcurl" )

using namespace std;
namespace zx = zeep::xml;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

class M6Curl
{
  public:
	static M6Curl&	Instance();
	
	void			Mirror(zx::element* inConfig);
	
  private:
					M6Curl();
					~M6Curl();

	class M6CurlFetch
	{
	  public:
						M6CurlFetch(zx::element* inConfig);
						~M6CurlFetch();
		
		void			Fetch();
	
	  private:	

		void			FetchFile(const string& inPath);
		
		time_t			FileDate(struct curl_fileinfo* inFInfo);

		long			FileIsComming(struct curl_fileinfo *finfo, int remains);
		long			FileIsDownloaded();
		size_t			WriteFile(char *buff, size_t size, size_t nmemb);

		static long		FileIsCommingCB(struct curl_fileinfo *finfo, M6CurlFetch* self, int remains);
		static long		FileIsDownloadedCB(M6CurlFetch* self);
		static size_t	WriteFileCB(char *buff, size_t size, size_t nmemb, M6CurlFetch* self);

		CURL*					mHandle;
		unique_ptr<M6Progress>	mProgress;
		unique_ptr<ostream>		mFile;
		zx::element*			mConfig;
		fs::path				mDstDir;
		string					mDir;
		deque<string>			mDirsToScan;
		deque<fs::path>			mFilesToFetch;
		int64					mBytesToFetch;
	};
};

// --------------------------------------------------------------------

M6Curl::M6CurlFetch::M6CurlFetch(zx::element* inConfig)
	: mHandle(curl_easy_init()), mConfig(inConfig->find_first("fetch"))
{
	if (mHandle == nullptr)
		THROW(("Out of memory? curl easy init failed"));

	if (mConfig == nullptr)
		THROW(("No fetch information available for databank "));

	string dst = mConfig->get_attribute("dst");
	if (dst.empty())
		dst = inConfig->get_attribute("id");
	
	fs::path rawdir = M6Config::Instance().FindGlobal("/m6-config/rawdir");
	mDstDir = rawdir / dst;
	
	if (not fs::exists(mDstDir))
		fs::create_directories(mDstDir);
	else if (not fs::is_directory(mDstDir))
		THROW(("Destination for fetch should be a directory"));
}

M6Curl::M6CurlFetch::~M6CurlFetch()
{
	curl_easy_cleanup(mHandle);
}

void M6Curl::M6CurlFetch::Fetch()
{
	string src = mConfig->get_attribute("src");
	
	mDirsToScan.push_back("");
	mBytesToFetch = 0;
	mFilesToFetch.clear();
	
	while (not mDirsToScan.empty())
	{
		mDir = mDirsToScan.front();
		mDirsToScan.pop_front();

		string src = mConfig->get_attribute("src");
		if (not mDir.empty())
			src = src + '/' + mDir;
		
		curl_easy_setopt(mHandle, CURLOPT_CHUNK_BGN_FUNCTION, (void*)&M6CurlFetch::FileIsCommingCB);
		curl_easy_setopt(mHandle, CURLOPT_CHUNK_END_FUNCTION, (void*)&M6CurlFetch::FileIsDownloadedCB);
		curl_easy_setopt(mHandle, CURLOPT_CHUNK_DATA, this);
		curl_easy_setopt(mHandle, CURLOPT_WRITEFUNCTION, (void*)&M6CurlFetch::WriteFileCB);
		curl_easy_setopt(mHandle, CURLOPT_WRITEDATA, this);
	
		if (mConfig->get_attribute("recurse") == "true" or
			mConfig->get_attribute("filter").empty() == false)
		{
			src = src + "/*";
		}

		if (VERBOSE)
			curl_easy_setopt(mHandle, CURLOPT_VERBOSE, 1L);
		
		curl_easy_setopt(mHandle, CURLOPT_WILDCARDMATCH, 1L);
		curl_easy_setopt(mHandle, CURLOPT_URL, src.c_str());
	
		CURLcode rc = curl_easy_perform(mHandle);
		if (rc != CURLE_OK)
			THROW(("Curl error: %s", curl_easy_strerror(rc)));
	}
	
	if (not mFilesToFetch.empty())
		mProgress.reset(new M6Progress(mBytesToFetch, "fetching"));

	foreach (fs::path& file, mFilesToFetch)
	{
		fs::path tmpFile(mDstDir / file.branch_path() / (file.filename().string() + ".tmp"));
		
		try
		{
			mFile.reset(new fs::ofstream(tmpFile, ios::binary));
			
			string src = mConfig->get_attribute("src") + '/' + file.string();
			
			curl_easy_setopt(mHandle, CURLOPT_WRITEFUNCTION, (void*)&M6CurlFetch::WriteFileCB);
			curl_easy_setopt(mHandle, CURLOPT_WRITEDATA, this);
		
			if (VERBOSE)
				curl_easy_setopt(mHandle, CURLOPT_VERBOSE, 1L);
			
			curl_easy_setopt(mHandle, CURLOPT_URL, src.c_str());
		
			CURLcode rc = curl_easy_perform(mHandle);
			if (rc != CURLE_OK)
				THROW(("Curl error: %s", curl_easy_strerror(rc)));
		}
		catch (...)
		{
			mFile.reset(nullptr);
			boost::system::error_code err;
			fs::remove(tmpFile, err);
			
			throw;
		}
		
		mFile.reset(nullptr);
	}
	
	mProgress.reset(nullptr);
}

long M6Curl::M6CurlFetch::FileIsCommingCB(struct curl_fileinfo *finfo,
	M6CurlFetch* self, int remains)
{
	return self->FileIsComming(finfo, remains);
}

long M6Curl::M6CurlFetch::FileIsDownloadedCB(M6CurlFetch* self)
{
	return self->FileIsDownloaded();
}

size_t M6Curl::M6CurlFetch::WriteFileCB(char *buff, size_t size, size_t nmemb, M6CurlFetch* self)
{
	return self->WriteFile(buff, size, nmemb);
}

time_t M6Curl::M6CurlFetch::FileDate(struct curl_fileinfo* inFInfo)
{
	time_t result = inFInfo->time;

	if (result == 0 and inFInfo->strings.time != nullptr)
	{
		time_t now = time(nullptr);
		result = curl_getdate(inFInfo->strings.time, &now);

		if (result <= 0)
		{
			boost::regex re("^(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +(\\d+) +(\\d+)(?::(\\d+))?$");
			boost::cmatch m;
			if (boost::regex_match(inFInfo->strings.time, m, re))
			{
				struct tm t;
#ifdef _MSC_VER
				gmtime_s(&t, &now);
#else
				gmtime_r(&now, &t);
#endif

				string time;
				if (m[4].str().empty())
					time = (boost::format("%2.2d %3.3s %4.4d") % m[2] % m[1] % m[3]).str();
				else
					time = (boost::format("%2.2d %3.3s %4.4d %2.2d:%2.2d") % m[2] % m[1] % (1900 + t.tm_year) % m[3] % m[4]).str();

				result = curl_getdate(time.c_str(), &now);
			}
		}
	}
	
	return result;
}

long M6Curl::M6CurlFetch::FileIsComming(struct curl_fileinfo *finfo, int remains)
{
	long result = CURL_CHUNK_BGN_FUNC_SKIP;

	fs::path file = fs::path(mDir) / finfo->filename;
	string filter = mConfig->get_attribute("filter");
	
	if (finfo->filetype == CURLFILETYPE_DIRECTORY and mConfig->get_attribute("recurse") == "true")
		mDirsToScan.push_back(mDir + '/' + finfo->filename);
	else if (finfo->filetype == CURLFILETYPE_FILE and (filter.empty() or M6FilePathNameMatches(file.filename(), filter)))
	{
		if (not fs::exists(mDstDir / file) or fs::last_write_time(mDstDir / file) < FileDate(finfo))
		{
			mFilesToFetch.push_back(file);
			mBytesToFetch += finfo->size;
		}
	}
		
	return result;
}

long M6Curl::M6CurlFetch::FileIsDownloaded()
{
	if (mProgress and mFile)
	{
		mProgress.reset(nullptr);
		mFile.reset(nullptr);
	}
	
	return CURL_CHUNK_END_FUNC_OK;
}

size_t M6Curl::M6CurlFetch::WriteFile(char *buff, size_t size, size_t nmemb)
{
	if (mFile and mProgress)
	{
		mFile->write(buff, size * nmemb);
		mProgress->Consumed(size * nmemb);
	}
	return size * nmemb;
}

// --------------------------------------------------------------------

M6Curl& M6Curl::Instance()
{
	static M6Curl sInstance;
	return sInstance;
}

M6Curl::M6Curl()
{
	curl_global_init(CURL_GLOBAL_ALL);
}

M6Curl::~M6Curl()
{
	curl_global_cleanup();
}

void M6Curl::Mirror(zx::element* inConfig)
{
	M6CurlFetch fetcher(inConfig);
	fetcher.Fetch();
}

void M6Fetch(const string& inDatabank)
{
	zx::element* config = M6Config::Instance().LoadDatabank(inDatabank);
	if (not config)
		THROW(("Configuration for %s is missing", inDatabank.c_str()));

	M6Curl::Instance().Mirror(config);
}
