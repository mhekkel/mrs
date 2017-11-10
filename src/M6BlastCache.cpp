//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <iostream>
#include <set>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/current_function.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/thread.hpp>
#include <boost/format.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string.hpp>

#include "M6Config.h"
#include "M6Error.h"
#include "M6BlastCache.h"
#include "M6Log.h"

using namespace std;

namespace fs = boost::filesystem;
namespace io = boost::iostreams;
namespace ba = boost::algorithm;
namespace zx = zeep::xml;

const uint32 kMaxCachedEntryResults = 1000;
const char* kBlastFileExtensions[] = { ".xml.bz2", ".job", ".err" };

// --------------------------------------------------------------------

bool M6BlastJob::IsJobFor(const string& inDatabank, const string& inQuery, const string& inProgram,
    const string& inMatrix, uint32 inWordSize, double inExpect, bool inLowComplexityFilter,
    bool inGapped, int32 inGapOpen, int32 inGapExtend, uint32 inReportLimit) const
{
    return db == inDatabank and
        query == inQuery and
        program == inProgram and
        matrix == inMatrix and
        wordsize == inWordSize and
        expect == inExpect and
        filter == inLowComplexityFilter and
        gapped == inGapped and
        gapOpen == inGapOpen and
        gapExtend == inGapExtend and
        reportLimit >= inReportLimit;        // <-- report limit at least inReportLimit
}

bool M6BlastJob::IsStillValid(const vector<fs::path>& inFiles) const
{
    vector<M6BlastDbInfo> fileInfo;

    for (const fs::path& file : inFiles)
    {
        M6BlastDbInfo info = { file.string(), boost::posix_time::from_time_t(fs::last_write_time(file)) };
        fileInfo.push_back(info);
    }

    return files == fileInfo;
}

// --------------------------------------------------------------------

M6BlastCache& M6BlastCache::Instance()
{
    static M6BlastCache sInstance;
    return sInstance;
}

M6BlastCache::M6BlastCache()
{
    LOG(DEBUG,"Creating M6BlastCache");

    string s = M6Config::GetDirectory("blast");
    if (s.empty())
        THROW(("Missing blastdir configuration"));

    mCacheDir = fs::path(s);
    if (not fs::exists(mCacheDir))
        fs::create_directory(mCacheDir);

    // read cached entries in result cache (unspecified order)
    fs::directory_iterator end;
    for (fs::directory_iterator iter(mCacheDir); iter != end; ++iter)
    {
        if (iter->path().extension().string() == ".job" and iter->path().filename().string().length() > 4)
        {
            fs::ifstream file(iter->path());
            if (not file.is_open())
            {
                LOG(WARN,"unable to open: %s",iter->path().extension().string().c_str());
                continue;
            }

            try // Must verify that the file's contents are OK, otherwise MRS will crash on startup
            {
                CacheEntry e;

                e.id = iter->path().filename().stem().string();
                e.hitCount = 0;
                e.bestScore = -1;

                zeep::xml::document doc(file);
                doc.deserialize("blastjob", e.job);

                if (fs::exists(mCacheDir / (e.id + ".err")))
                    e.status = bj_Error;
                else if (fs::exists(mCacheDir / (e.id + ".xml.bz2")))
                    e.status = bj_Finished;
                else
                    e.status = bj_Queued;

                mResultCache.push_back(e);
            }
            catch (exception& ex)
            {
                LOG(WARN,"Cannot parse job file %s: %s",iter->path().string().c_str(),ex.what());
                fs::remove(iter->path());
            }
        }
    }

    // Determine the number of threads from config:
    const zx::element *server = M6Config::GetServer(),
                      *blaster = server->find_first("blaster");
    mMaxThreads = boost::thread::hardware_concurrency();
    if (blaster != nullptr)
    {
        uint32 n =  boost::lexical_cast<uint32> (blaster->get_attribute ("nthread"));
        if (n > 0)
            mMaxThreads = n;
    }

    // finally start the worker threads
    mStopWorkingFlag = false;
    mWorkerThread = boost::thread([this](){ this->Work (); });
    mHighLoadThread = boost::thread([this](){ this->Work (true); });

    LOG(DEBUG,"M6BlastCache has been created");
}

M6BlastCache::~M6BlastCache()
{
    mStopWorkingFlag = true;

    if (mWorkerThread.joinable())
    {
        mWorkerThread.interrupt();
        mWorkerThread.join();
    }
}

string StatusToString(M6BlastJobStatus status) {

    switch(status) {
    case bj_Unknown:
        return "unknown";
    case bj_Queued:
        return "queued";
    case bj_Running:
        return "running";
    case bj_Finished:
        return "finished";
    case bj_Error:
        return "error";
    default:
        return to_string (status);
    }
}

tuple<M6BlastJobStatus,string,uint32,double> M6BlastCache::JobStatus(const string& inJobID)
{
    boost::mutex::scoped_lock lock(mCacheMutex);

    tuple<M6BlastJobStatus,string,uint32,double> result;

    get<0>(result) = bj_Unknown;

    LOG (DEBUG,"M6BlastCache::JobStatus: looking up job \"%s\" in cache",inJobID.c_str());

    auto i = find_if(mResultCache.begin(), mResultCache.end(), [&inJobID](CacheEntry& e) -> bool
                { return e.id == inJobID; });

    if (i != mResultCache.end()) // is requested job id in list ?
    {
        LOG (DEBUG,"M6BlastCache::JobStatus: matched job \"%s\" with status %s",
		   inJobID.c_str (), StatusToString (i->status).c_str ());

        get<0>(result) = i->status;

        try
        {
            if (i->status == bj_Finished)
            {
                if (i->bestScore < 0)
                {
                    M6BlastResultPtr jobResult = JobResult(inJobID);

                    if (jobResult)
                    {
                        i->hitCount = static_cast<uint32>(jobResult->mHits.size());
                        if (not jobResult->mHits.empty() and not jobResult->mHits.front().mHsps.empty())
                            i->bestScore = jobResult->mHits.front().mHsps.front().mExpect;
                    }
                }

                get<2>(result) = i->hitCount;
                get<3>(result) = i->bestScore;
            }
            else if (i->status == bj_Error)
            {
                fs::path errPath(mCacheDir / (inJobID + ".err"));
                if (fs::exists(errPath))
                {
                    fs::ifstream file(errPath);
                    getline(file, get<1>(result));
                }
                else
                    get<1>(result) = "missing error message";
            }
        }
        catch (exception& ex)
        {
            get<0>(result) = bj_Error;
            get<1>(result) = ex.what();
        }

        // Place the job with requested id at the beginning of the list
        auto j = i;
        advance(j, 1);
        if (i != mResultCache.begin())
            mResultCache.splice(mResultCache.begin(), mResultCache, i, j);
    }

    LOG (DEBUG, "M6BlastCache::JobStatus: returning status %s for job %s",
		StatusToString (i->status).c_str (),
		inJobID.c_str ());

    return result;
}

M6BlastResultPtr M6BlastCache::JobResult(const string& inJobID)
{
    fs::path xmlPath = mCacheDir / (inJobID + ".xml.bz2");
    io::filtering_stream<io::input> in;
    fs::ifstream file(xmlPath, ios::binary);
    if (not file.is_open())
        throw M6Exception("missing blast result file");

    in.push(io::bzip2_decompressor());
    in.push(file);

    M6BlastResultPtr result(new M6Blast::Result);

    try
    {
        zeep::xml::document doc(in);
        doc.deserialize("blast-result", const_cast<M6Blast::Result&>(*result));
    }
    catch(exception &ex)
    {
        LOG(WARN,"Cannot parse result file %s: %s", xmlPath.string().c_str(), ex.what());
        fs::remove(xmlPath);
        SetJobStatus(inJobID, bj_Queued);
    }

    return result;
}

void M6BlastCache::CacheResult(const string& inJobID, M6BlastResultPtr inResult)
{
    // A job has just been completed and must be cached.

    boost::mutex::scoped_lock lock(mCacheMutex);

    auto i = find_if(mResultCache.begin(), mResultCache.end(), [&inJobID](CacheEntry& e) -> bool
                { return e.id == inJobID; });

    if (i != mResultCache.end()) // is requested job id in list ?
    {
        i->status = bj_Finished;

        i->hitCount = static_cast<uint32>(inResult->mHits.size());
        if (not inResult->mHits.empty() and not inResult->mHits.front().mHsps.empty())
            i->bestScore = inResult->mHits.front().mHsps.front().mExpect;
        else
            i->bestScore = 0;

        zeep::xml::document doc;
        doc.serialize("blast-result", *inResult);

        LOG(DEBUG,"Storing result of blast job with id: %s",inJobID.c_str());

        fs::ofstream file(mCacheDir / (inJobID + ".xml.bz2"), ios_base::out|ios_base::trunc|ios_base::binary);
        io::filtering_stream<io::output> out;

        if (not file.is_open())
            throw runtime_error("could not create output file");

        out.push(io::bzip2_compressor());
        out.push(file);

        out << doc;

        // Place the job with requested id at the beginning of the list
        auto j = i;
        advance(j, 1);
        if (i != mResultCache.begin())
            mResultCache.splice(mResultCache.begin(), mResultCache, i, j);

        LOG(DEBUG, "closing output file for blast job with id: %s",inJobID.c_str());
    }

    // do some housekeeping, jobs at the back are first to be removed.
    while (mResultCache.size() > kMaxCachedEntryResults)
    {
        for (const char* ext : kBlastFileExtensions)
        {
            boost::system::error_code ec;
            fs::remove(mCacheDir / (mResultCache.back().id + ext), ec);
        }

        mResultCache.pop_back();
    }
}

void M6BlastCache::FastaFilesForDatabank(const string& inDatabank, vector<fs::path>& outFiles)
{
    vector<string> dbs;
    ba::split(dbs, inDatabank, ba::is_any_of(";"));

    sort(dbs.begin(), dbs.end());

    for (string& db : dbs)
    {
        fs::path dbdir = M6Config::GetDbDirectory(db);

        if (not fs::exists(dbdir / "fasta"))
            THROW(("Databank '%s' does not contain a fasta file", db.c_str()));

        outFiles.push_back(dbdir / "fasta");
    }

    if (outFiles.empty())
        THROW(("Databank '%s' does not contain a fasta file", inDatabank.c_str()));
}

string M6BlastCache::Submit(const string& inDatabank, const string& inQuery,
    const string& inProgram, const string& inMatrix, uint32 inWordSize,
    double inExpect, bool inLowComplexityFilter,
    bool inGapped, int32 inGapOpen, int32 inGapExtend,
    uint32 inReportLimit)
{
    LOG(DEBUG,"M6BlastCache::Submit call with query length %d and databank %s",inQuery.size(),inDatabank.c_str());

    if (inReportLimit > 1000)
        THROW(("Report limit exceeds maximum of 1000 hits"));

    vector<fs::path> fastaFiles;
    FastaFilesForDatabank(inDatabank, fastaFiles);

    string result;
    boost::mutex::scoped_lock lock(mCacheMutex);

    // see if the job has already been submitted before
    for (CacheEntry& e : mResultCache)
    {
        if (not e.job.IsJobFor(inDatabank, inQuery, inProgram, inMatrix, inWordSize, inExpect,
                inLowComplexityFilter, inGapped, inGapOpen, inGapExtend, inReportLimit))
            continue;

        result = e.id;

        if ((e.status == bj_Finished or e.status == bj_Error) and not e.job.IsStillValid(fastaFiles))
        {
            e.status = bj_Queued;
            StoreJob(e.id, e.job);    // need to store the job again, since the timestamps changed

            mWorkCondition.notify_all();

            // clean up stale files
            if (fs::exists(mCacheDir / (e.id + ".xml.bz2")))
                fs::remove(mCacheDir / (e.id + ".xml.bz2"));

            if (fs::exists(mCacheDir / (e.id + ".err")))
                fs::remove(mCacheDir / (e.id + ".err"));

            // reset the timestamps
            e.job.files.clear();
            for (fs::path& fastaFile : fastaFiles)
            {
                M6BlastDbInfo info = { fastaFile.string(), boost::posix_time::from_time_t(fs::last_write_time(fastaFile)) };
                e.job.files.push_back(info);
            }
        }

        LOG(DEBUG,"M6BlastCache::Submit: returning existing blast job id: %s",result.c_str());

        break;
    }

    if (result.empty()) // new job, add to the queue
    {
        static boost::uuids::random_generator gen;

        CacheEntry e;

        e.id = boost::lexical_cast<string>(gen());
        e.status = bj_Queued;

        e.job.db = inDatabank;
        e.job.query = inQuery;
        e.job.program = inProgram;
        e.job.matrix = inMatrix;
        e.job.wordsize = inWordSize;
        e.job.expect = inExpect;
        e.job.filter = inLowComplexityFilter;
        e.job.gapped = inGapped;
        e.job.gapOpen = inGapOpen;
        e.job.gapExtend = inGapExtend;
        e.job.reportLimit = inReportLimit;

        for (fs::path& fastaFile : fastaFiles)
        {
            M6BlastDbInfo info = { fastaFile.string(), boost::posix_time::from_time_t(fs::last_write_time(fastaFile)) };
            e.job.files.push_back(info);
        }

        StoreJob(e.id, e.job);

        // New jobs are added to the front, because jobs at the back are first to be removed when the queue is full.
        mResultCache.push_front(e);
        mWorkCondition.notify_all();

        result = e.id;

        LOG(DEBUG,"M6BlastCache::Submit: returning newly created blast job id: %s",result.c_str());
    }

    return result;
}

void M6BlastCache::StoreJob(const string& inJobID, const M6BlastJob& inJob)
{
    // store the job in the cache directory
    zeep::xml::document doc;
    doc.serialize("blastjob", inJob);

    fs::ofstream file(mCacheDir / (inJobID + ".job"), ios_base::out|ios_base::trunc);
    file << doc;
}

bool IsHighLoad(const M6BlastJob &job)
{
    // query sequence length:
    if (job.query.length () > 1e4)
        return true;

    // Database files contain fasta format sequences (uncompressed)
    for (const M6BlastDbInfo& dbi : job.files)
    {
        uintmax_t size = fs::file_size (dbi.path);

        if (size > 1e9) // larger than a gigabyte
            return true;
    }

    return false;
}

void M6BlastCache::Work(const bool highload)
{
    using namespace boost::posix_time;

    boost::mutex::scoped_lock lock (highload? mHighLoadMutex: mWorkMutex);

    while (not mStopWorkingFlag)
    {
        try
        {
            string next;

            {
                // Pick the frontmost queued entry. An alternative would be to use reversed looping.
                boost::mutex::scoped_lock lock2(mCacheMutex);
                for (CacheEntry& e : mResultCache)
                {
                    if (e.status != bj_Queued)
                        continue;

                    /*
                        The highload thread must only do highload jobs,
                        the normal thread must only do normal jobs.
                     */
                    if (highload != IsHighLoad (e.job))
                        continue;

                    next = e.id;
                    break;
                }
            }

            if (next.empty()) // if no new job, wait
                mWorkCondition.wait(lock);
            else
                ExecuteJob (next, highload? 1: mMaxThreads - 1);
        }
        catch (boost::thread_interrupted&)
        {
            // just continue with the next queued job
        }
        catch (exception& e)
        {
            cerr << e.what() << endl;
            boost::this_thread::sleep(seconds(5));
        }
        catch (...)
        {
            cerr << "unknown exception" << endl;
            boost::this_thread::sleep(seconds(5));
        }
    }
}

bool M6BlastCache::LoadCacheJob(const std::string& inJobID, M6BlastJob& job)
{
    boost::mutex::scoped_lock lock(mCacheMutex);

    fs::ifstream file(mCacheDir / (inJobID + ".job"));
    if (file.is_open())
    {
        try
        {
            zeep::xml::document doc(file);
            doc.deserialize("blastjob", job);
        }
        catch(exception &ex)
        {
            LOG(ERROR, "cannot parse job %s: %s", inJobID.c_str(), ex.what());

            return false;
        }

        return true;
    }

    LOG(ERROR, "cannot missing job file for %s: %s", inJobID.c_str());

    return false;
}
void M6BlastCache::ExecuteJob(const string& inJobID, const uint32 n_threads)
{
    try
    {
        LOG(INFO, "executing blast job %s", inJobID.c_str());

        SetJobStatus(inJobID, bj_Running);

        M6BlastJob job;
        if (!LoadCacheJob (inJobID, job))
        {
            DeleteJob(inJobID);
            return;
        }

        vector<fs::path> files;
        transform(job.files.begin(), job.files.end(), back_inserter(files),
            [](M6BlastDbInfo& dbi) { return dbi.path; });

        M6BlastResultPtr result(M6Blast::Search(files, job.query, job.program,
            job.matrix, job.wordsize, job.expect, job.filter, job.gapped,
            job.gapOpen, job.gapExtend, job.reportLimit, n_threads));

        CacheResult(inJobID, result);

        LOG(INFO,"completed blast job %s", inJobID.c_str());
    }
    catch (exception& e)
    {
        LOG(DEBUG,"M6BlastCache::ExecuteJob: exception thrown for job %s: %s",inJobID.c_str(),e.what());

        SetJobStatus(inJobID, bj_Error);

        fs::ofstream file(mCacheDir / (inJobID + ".err"));
        if (file.is_open())    // silenty ignore errors.... what else?
            file << e.what();

        throw;
    }
}

void M6BlastCache::SetJobStatus(const string inJobId, M6BlastJobStatus inStatus)
{
    boost::mutex::scoped_lock lock(mCacheMutex);

    for (CacheEntry& e : mResultCache)
    {
        if (e.id != inJobId)
            continue;

        e.status = inStatus;
        break;
    }
}

void M6BlastCache::Purge(bool inDeleteFiles)
{
    if (inDeleteFiles)
    {
        // TODO implement
    }
}

M6BlastJobDescList M6BlastCache::GetJobList()
{
    boost::mutex::scoped_lock lock(mCacheMutex);

    M6BlastJobDescList result;

    for (CacheEntry& e : mResultCache)
    {
        M6BlastJobDesc desc;

        desc.id = e.id;
        desc.db = e.job.db;
        desc.queryLength = static_cast<uint32>(e.job.query.length());

        switch (e.status)
        {
            case bj_Unknown:    desc.status = "unknown"; break;
            case bj_Error:        desc.status = "error"; break;
            case bj_Queued:        desc.status = "queued"; break;
            case bj_Running:    desc.status = "running"; break;
            case bj_Finished:    desc.status = "finished"; break;
        }

        result.push_back(desc);
    }

    return result;
}

void M6BlastCache::DeleteJob(const string& inJobID)
{
    try
    {
        (void)boost::lexical_cast<boost::uuids::uuid>(inJobID);
    }
    catch (boost::bad_lexical_cast&)
    {
        THROW(("Invalid job id"));
    }

    boost::mutex::scoped_lock lockdb(mCacheMutex);

    auto c = find_if(mResultCache.begin(), mResultCache.end(),
        [&inJobID](CacheEntry& e) -> bool { return e.id == inJobID; });

    if (c != mResultCache.end()) // if the ID was found, remove the job from the list
        mResultCache.erase(c);

    if (mWorkerThread.joinable())
        mWorkerThread.interrupt();

    for (const char* ext : kBlastFileExtensions) // remove job files from the cache directory
    {
        boost::system::error_code ec;
        fs::remove(mCacheDir / (inJobID + ext), ec);
    }

    mWorkCondition.notify_all();
}
