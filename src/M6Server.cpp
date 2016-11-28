//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"
#include "M6Log.h"

#if ! defined(_MSC_VER)
#include <signal.h>
#include <sys/resource.h>
#endif

#include <iostream>
#include <numeric>
#include <cmath>

#include <boost/bind.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/random/random_device.hpp>
#include <boost/chrono.hpp>

#include <zeep/envelope.hpp>

#include "M6Utilities.h"
#include "M6Databank.h"
#include "M6Server.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"
#include "M6Config.h"
#include "M6Query.h"
#include "M6Tokenizer.h"
#include "M6Builder.h"
#include "M6MD5.h"
#include "M6BlastCache.h"
#include "M6Exec.h"
#include "M6Parser.h"
#include "M6Progress.h"
#include "M6WSSearch.h"
#include "M6WSBlast.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace pt = boost::posix_time;
namespace po = boost::program_options;

const string kM6ServerNS = "http://mrs.cmbi.ru.nl/mrs-web/ml";

// --------------------------------------------------------------------

struct M6Redirect
{
    string    db;
    uint32    nr;
};

// --------------------------------------------------------------------

M6Server* M6Server::sInstance;

M6Server::M6Server(const zx::element* inConfig)
    : webapp(kM6ServerNS)
    , mConfig(inConfig)
    , mAlignEnabled(false)
    , mConfigCopy(nullptr)
{
    LOG(INFO,"M6Server: loading databanks..");

    LoadAllDatabanks();

    LOG(INFO,"M6Server: done loading databanks");

    set_docroot(M6Config::GetDirectory("docroot"));

    log_forwarded(mConfig->get_attribute("log-forwarded") == "true");

    mount("",                boost::bind(&M6Server::handle_welcome, this, _1, _2, _3));
    mount("download",        boost::bind(&M6Server::handle_download, this, _1, _2, _3));
    mount("entry",            boost::bind(&M6Server::handle_entry, this, _1, _2, _3));
    mount("link",            boost::bind(&M6Server::handle_link, this, _1, _2, _3));
    mount("linked",            boost::bind(&M6Server::handle_linked, this, _1, _2, _3));
    mount("search",            boost::bind(&M6Server::handle_search, this, _1, _2, _3));
    mount("similar",        boost::bind(&M6Server::handle_similar, this, _1, _2, _3));
    mount("scripts",        boost::bind(&M6Server::handle_file, this, _1, _2, _3));
    mount("formats",        boost::bind(&M6Server::handle_file, this, _1, _2, _3));
    mount("css",            boost::bind(&M6Server::handle_file, this, _1, _2, _3));
//    mount("man",            boost::bind(&M6Server::handle_file, this, _1, _2, _3));
    mount("images",            boost::bind(&M6Server::handle_file, this, _1, _2, _3));

    mount("ajax/search",    boost::bind(&M6Server::handle_search_ajax, this, _1, _2, _3));

    mount("rest",            boost::bind(&M6Server::handle_rest, this, _1, _2, _3));

    mount("favicon.ico",    boost::bind(&M6Server::handle_file, this, _1, _2, _3));
    mount("robots.txt",        boost::bind(&M6Server::handle_file, this, _1, _2, _3));

    mount("blast",                boost::bind(&M6Server::handle_blast, this, _1, _2, _3));
    mount("ajax/blast/submit",    boost::bind(&M6Server::handle_blast_submit_ajax, this, _1, _2, _3));
    mount("ajax/blast/status",    boost::bind(&M6Server::handle_blast_status_ajax, this, _1, _2, _3));
    mount("ajax/blast/result",    boost::bind(&M6Server::handle_blast_results_ajax, this, _1, _2, _3));

    mount("align",                boost::bind(&M6Server::handle_align, this, _1, _2, _3));
    mount("ajax/align/submit",    boost::bind(&M6Server::handle_align_submit_ajax, this, _1, _2, _3));

    mount("status",            boost::bind(&M6Server::handle_status, this, _1, _2, _3));
    mount("ajax/status",    boost::bind(&M6Server::handle_status_ajax, this, _1, _2, _3));
    mount("info",            boost::bind(&M6Server::handle_info, this, _1, _2, _3));
    mount("browse",            boost::bind(&M6Server::handle_browse, this, _1, _2, _3));

    zx::node* realm = mConfig->find_first_node("admin/@realm");
    if (realm == nullptr)
    {
        mount("admin", boost::bind(&M6Server::handle_admin, this, _1, _2, _3));
        mount("ajax/blast/queue", boost::bind(&M6Server::handle_admin_blast_queue_ajax, this, _1, _2, _3));
        mount("ajax/blast/delete", boost::bind(&M6Server::handle_admin_blast_delete_ajax, this, _1, _2, _3));
    }
    else
    {
        mount("admin", realm->str(), boost::bind(&M6Server::handle_admin, this, _1, _2, _3));
        mount("ajax/blast/queue", realm->str(), boost::bind(&M6Server::handle_admin_blast_queue_ajax, this, _1, _2, _3));
        mount("ajax/blast/delete", realm->str(), boost::bind(&M6Server::handle_admin_blast_delete_ajax, this, _1, _2, _3));
    }

    LOG(DEBUG,"M6Server: add processors");

    add_processor("link",    boost::bind(&M6Server::process_mrs_link, this, _1, _2, _3));
    add_processor("enable",    boost::bind(&M6Server::process_mrs_enable, this, _1, _2, _3));

    if (zx::element* e = mConfig->find_first("base-url"))
    {
        mBaseURL = e->content();
        if (not ba::ends_with(mBaseURL, "/"))
            mBaseURL += '/';
    }

    LOG(DEBUG,"M6Server: getting clustal");

    fs::path clustalo(M6Config::GetTool("clustalo"));
    if (fs::exists(clustalo))
        mAlignEnabled = true;

    LOG(DEBUG,"M6Server: mounting web services");

    // web services:
    for (zx::element* ws : mConfig->find("web-service"))
    {
        string service = ws->get_attribute("service");
        string ns = ws->get_attribute("ns");
        string location = ws->get_attribute("location");

        zeep::dispatcher* d = nullptr;
        if (service == "mrsws_search")
            d = new M6WSSearch(*this, mLoadedDatabanks, ns, service);
        else if (service == "mrsws_blast")
            d = new M6WSBlast(*this, ns, service);
        else
            THROW(("Invalid web service specified: %s", service.c_str()));

        mWebServices.push_back(d);

        mount(location, [this, d] (const zh::request& request, const el::scope& scope, zh::reply& reply)
        {
            try
            {
                zx::document doc;
                doc.read(request.payload);
                zeep::envelope env(doc);

                zx::element* request = env.request();
                reply.set_content(zeep::make_envelope(d->dispatch(request)));
                log() << request->name();
            }
            catch (exception& e)
            {
                if (request.method == "POST")
                {
                    reply.set_content(zeep::make_fault(e));
                    log() << "SOAP Fault";
                }
                else
                {
                    el::scope scope(request);
                    init_scope(scope);

                    scope.put("errormsg", "This is a SOAP server, please POST a valid SOAP request.");

                    create_reply_from_template("error.html", scope, reply);
                }
            }
        });

        string wsdl = location + "/wsdl";
        location = mBaseURL + location;

        mount(wsdl, [d, location] (const zh::request& request, const el::scope& scope, zh::reply& reply)
        {
            reply.set_content(d->make_wsdl(location));
        });
    }

    LOG(DEBUG,"M6Server: setting instance");

    if (sInstance && sInstance != this)
        delete sInstance;
    sInstance = this;

    LOG(DEBUG,"M6Server: done");
}

M6Server::~M6Server()
{
    for (M6LoadedDatabank& db : mLoadedDatabanks)
    {
        delete db.mDatabank;
        delete db.mParser;
    }

    for (zeep::dispatcher* ws : mWebServices)
        delete ws;

    delete mConfigCopy;

    if (sInstance == this)
        sInstance = nullptr;
}

void M6Server::LoadAllDatabanks()
{
    mLinkMap.clear();

    map<string,set<string>> blastAliases;
    map<string,string> names;

    for (zx::element* config : M6Config::GetDatabanks())
    {
        try
        {
            if (config->get_attribute("enabled") != "true")
                continue;

            string databank = config->get_attribute("id");

            fs::path dbdir = M6Config::GetDbDirectory(databank);
            if (not fs::exists(dbdir) or not fs::is_directory(dbdir))
            {
                if (VERBOSE)
                    cerr << "databank " << databank << " not available" << endl;
                continue;
            }

            fs::path fasta(dbdir / "fasta");
            bool blast = fs::exists(fasta);
            if (blast)
                blastAliases[databank].insert(databank);

            string name = databank;
            if (zx::element* n = config->find_first("name"))
                name = n->content();

            if (names[databank].empty() and not name.empty())
                names[databank] = name;

            M6Parser* parser = nullptr;
            if (not config->get_attribute("parser").empty())
                parser = new M6Parser(config->get_attribute("parser"));

            set<string> aliases;
            for (zx::element* alias : config->find("aliases/alias"))
            {
                string s(alias->content());
                M6Tokenizer::CaseFold(s);
                aliases.insert(s);
                if (names[s].empty() and not alias->get_attribute("name").empty())
                    names[s] = alias->get_attribute("name");
                if (blast)
                    blastAliases[s].insert(databank);
            }

            M6LoadedDatabank ldb =
            {
                new M6Databank(dbdir, eReadOnly),
                databank,
                name,
                aliases,
                blast,
                parser
            };

            mLoadedDatabanks.push_back(ldb);

            mLinkMap[databank].insert(ldb.mDatabank);
            for (const string& alias : aliases)
                mLinkMap[alias].insert(ldb.mDatabank);
        }
        catch (exception& e)
        {
            cerr << "Error loading databank " << config->get_attribute("id") << endl
                 << " >> " << e.what() << endl;
        }
    }

    for (M6LoadedDatabank& db : mLoadedDatabanks)
        db.mDatabank->InitLinkMap(mLinkMap);

    // setup the mBlastDatabanks list
    for (auto& blastAlias : blastAliases)
    {
        M6BlastDatabank bdb = { blastAlias.first, names[blastAlias.first] };

        if (bdb.mName.empty())    // refuse empty names
            continue;

        for (const string& f : blastAlias.second)
            bdb.mIDs.insert(f);
        mBlastDatabanks.push_back(bdb);
    }
}

M6Databank* M6Server::Load(const string& inDatabank)
{
    M6Databank* result = nullptr;

    string databank(inDatabank);
    M6Tokenizer::CaseFold(databank);

    for (M6LoadedDatabank& db : mLoadedDatabanks)
    {
        if (db.mID == databank)
        {
            result = db.mDatabank;
            break;
        }
    }

    return result;
}

tuple<M6Databank*,uint32> M6Server::GetEntryDatabankAndNr(const string& inDatabank, const string& inID)
{
    string db = inDatabank;

    string id = inID;
    M6Tokenizer::CaseFold(id);

    M6Databank* mdb = Load(db);
    uint32 docNr = 0;

    if (mdb == nullptr and not id.empty())
    {
        for (string adb : UnAlias(db))
        {
            mdb = Load(adb);
            if (mdb != nullptr)
            {
                bool exists;
                tie(exists, docNr) = mdb->Exists("id", id);
                if (exists)
                {
                    db = adb;
                    break;
                }
            }
        }
    }

    if (mdb == nullptr)
        THROW(("Databank %s not loaded", db.c_str()));

    if (docNr == 0)
    {
        bool exists;
        tie(exists, docNr) = mdb->Exists("id", id);
        if (not exists)
            THROW(("Entry %s does not exist in databank %s", id.c_str(), db.c_str()));
        if (docNr == 0)
            THROW(("Multiple entries with ID %s in databank %s", id.c_str(), db.c_str()));
    }

    return make_tuple(mdb, docNr);
}

string M6Server::GetEntry(M6Databank* inDatabank, const string& inFormat, uint32 inDocNr)
{
    unique_ptr<M6Document> doc(inDatabank->Fetch(inDocNr));
    if (not doc)
        THROW(("Unable to fetch document"));

    string result;

    if (inFormat == "title")
        result = doc->GetAttribute("title");
    else
    {
        result = doc->GetText();

        if (inFormat == "fasta")
        {
            for (M6LoadedDatabank& db : mLoadedDatabanks)
            {
                if (db.mDatabank != inDatabank or db.mParser == nullptr)
                    continue;

                string fasta;
                db.mParser->ToFasta(result, db.mID, doc->GetAttribute("id"),
                    doc->GetAttribute("title"), fasta);

                result = fasta;
                break;
            }
        }
    }

    return result;
}

string M6Server::GetEntry(M6Databank* inDatabank,
    const string& inFormat, const string& inIndex, const string& inValue)
{
    unique_ptr<M6Iterator> iter(inDatabank->Find(inIndex, inValue));
    uint32 docNr;
    float rank;

    if (not (iter and iter->Next(docNr, rank)))
        THROW(("Entry %s not found", inValue.c_str()));

    return GetEntry(inDatabank, inFormat, docNr);
}

string M6Server::GetEntry(const string& inDB, const string& inID, const string& inFormat)
{
    M6Databank* db;
    uint32 docNr;

    string id(inID), chain;
    string::size_type s;

    if (inFormat == "fasta" and (s = id.find('/')) != string::npos)
    {
        id = inID.substr(0, s);
        chain = inID.substr(s + 1);
    }

    tie(db, docNr) = GetEntryDatabankAndNr(inDB, id);

    string result = GetEntry(db, inFormat, docNr);

    // in case we only need one chain in the fasta formatted entry
    if (not chain.empty())
    {
        istringstream rs(result);
        result.clear();

        const string rxs("^>gnl\\|");
        boost::regex rx(rxs + inDB + "\\|" + id + "\\|" + chain + "(?: .*)?", boost::regex::icase);

        enum { search, copy, done } state = search;

        while (state != done)
        {
            string line;
            getline(rs, line);
            if (line.empty() and rs.eof())
                break;

            switch (state)
            {
                case search:
                    if (boost::regex_match(line, rx))
                    {
                        result = line + '\n';
                        state = copy;
                    }
                    break;

                case copy:
                    if (ba::starts_with(line, ">"))
                        state = done;
                    else
                        result += line + '\n';
                    break;

                case done: break;
            }
        }
    }

    return result;
}

void M6Server::Find(const string& inDatabank, const string& inQuery, bool inAllTermsRequired,
    uint32 inResultOffset, uint32 inMaxResultCount, bool inAddLinks,
    vector<el::object>& outHits, uint32& outHitCount, bool& outRanked,
    string& outParseError)
{
    M6Databank* databank = Load(inDatabank);

    if (databank == nullptr)
        THROW(("Invalid databank"));

    if (inQuery.length() > 1024)
        THROW(("Query too long"));

    if (inResultOffset >= databank->size())    // no hits left
        return;

    unique_ptr<M6Iterator> rset;
    M6Iterator* filter = nullptr;
    vector<string> queryTerms;
    bool isBooleanQuery = false;

    try
    {
        ParseQuery(*databank, inQuery, inAllTermsRequired, queryTerms, filter, isBooleanQuery);
    }
    catch (exception& e)
    {
        outParseError = e.what();

        stringstream q;
        M6Tokenizer tokenizer(inQuery);
        for (;;)
        {
            M6Token token = tokenizer.GetNextWord();
            if (token == eM6TokenEOF)
                break;

            if (token == eM6TokenWord or token == eM6TokenNumber)
                q << tokenizer.GetTokenString() << ' ';
        }

        ParseQuery(*databank, q.str(), inAllTermsRequired, queryTerms, filter, isBooleanQuery);
    }

    if (isBooleanQuery)
        inAllTermsRequired = false;

    if (queryTerms.empty())
        rset.reset(filter);
    else
        rset.reset(databank->Find(queryTerms, filter, inAllTermsRequired, numeric_limits<uint32>::max()));//inResultOffset + inMaxResultCount));
        // We want to report the total number of hits, thus no report limit here!

    if (not rset or rset->GetCount() == 0)
        outHitCount = 0;
    else
    {
        outHitCount = rset->GetCount(); // can be wrong !
        outRanked = rset->IsRanked();

        uint32 docNr, nr = 1;

        float score = 0;

        while (inResultOffset-- > 0 and rset->Next(docNr, score))
            ++nr;

        while (inMaxResultCount-- > 0 and rset->Next(docNr, score))
        {
            unique_ptr<M6Document> doc(databank->Fetch(docNr));
            if (not doc)
                THROW(("Unable to fetch document %d", docNr));

            string id = doc->GetAttribute("id");

            el::object hit;
            hit["nr"] = nr;
            hit["docNr"] = docNr;
            hit["id"] = id;
            hit["title"] = doc->GetAttribute("title");
            hit["score"] = static_cast<uint16>(score * 100);

            if (inAddLinks)
                AddLinks(inDatabank, id, hit);

            outHits.push_back(hit);
            ++nr;
        }

        // count the number of hits in rset
        outHitCount = nr - 1;
        while (rset->Next(docNr, score)) outHitCount++;
    }
}

uint32 M6Server::Count(const string& inDatabank, const string& inQuery)
{
    uint32 result = 0;

    if (inDatabank == "all")        // same as count for all databanks
    {
        for (M6LoadedDatabank& db : mLoadedDatabanks)
            result += Count(db.mID, inQuery);
    }
    else
    {
        for (const string& databank : UnAlias(inDatabank))
        {
            unique_ptr<M6Iterator> rset;
            M6Iterator* filter;
            vector<string> queryTerms;
            bool isBooleanQuery;

            M6Databank* db = Load(databank);
            if (db == nullptr)
                THROW(("Databank %s not loaded", databank.c_str()));

            if (inQuery == "*")
                result += db->size();
            else
            {
                ParseQuery(*db, inQuery, true, queryTerms, filter, isBooleanQuery);
                if (queryTerms.empty())
                    rset.reset(filter);
                else
                    rset.reset(db->Find(queryTerms, filter, not isBooleanQuery, 1));

                if (rset) {

                    uint32 docNr; float score;
                    while (rset->Next(docNr, score)) result++;
                }
            }
        }
    }

    return result;
}

// --------------------------------------------------------------------

vector<string> M6Server::UnAlias(const string& inDatabank)
{
    vector<string> result;

    LOG (DEBUG, "attempting to resolve alias %s", inDatabank.c_str ());

    for (auto& db : mLoadedDatabanks)
    {
        if (db.mID == inDatabank or db.mAliases.count(inDatabank))
            result.push_back(db.mID);
    }

    sort(result.begin(), result.end());
    result.erase(unique(result.begin(), result.end()), result.end());

    LOG (DEBUG, "found %d databanks for alias %s", result.size (),
                                                   inDatabank.c_str ());

    return result;
}

vector<string> M6Server::GetAliases(const string& inDatabank)
{
    vector<string> result;

    result.push_back(inDatabank);

    for (auto& db : mLoadedDatabanks)
    {
        if (db.mID == inDatabank)
        {
            result.insert(result.end(), db.mAliases.begin(), db.mAliases.end());
            break;
        }
    }

    sort(result.begin(), result.end());
    result.erase(unique(result.begin(), result.end()), result.end());

    return result;
}

void M6Server::GetLinkedDbs(const string& inDb, const string& inId,
    vector<string>& outLinkedDbs)
{
    set<string> dbs;

    string id(inId);
    M6Tokenizer::CaseFold(id);

    M6Databank* databank = Load(inDb);
    if (databank != nullptr)
    {
        unique_ptr<M6Document> doc(databank->Fetch(id));
        if (doc)
        {
            for (const auto& l : doc->GetLinks())
            {
                vector<string> aliases(UnAlias(l.first));

                for (const string& alias : aliases)
                {
                    if (dbs.count(alias))
                        continue;

                    M6Databank* db = Load(alias);
                    if (db == nullptr)
                        continue;

                    for (const string& id : l.second)
                    {
                        bool exists;
                        uint32 docNr;
                        tie(exists, docNr) = db->Exists("id", id);
                        if (exists)
                        {
                            dbs.insert(alias);
                            break;
                        }
                    }
                }
            }
        }
    }

    vector<string> aliases(GetAliases(inDb));

    for (auto& db : mLoadedDatabanks)
    {
        if (dbs.count(db.mID))
            continue;

        for (auto& alias : aliases)
        {
            if (db.mDatabank->IsLinked(alias, inId))
            {
                dbs.insert(db.mID);
                break;
            }
        }
    }

    outLinkedDbs.assign(dbs.begin(), dbs.end());
}

void M6Server::AddLinks(const string& inDB, const string& inID, el::object& inHit)
{
    vector<string> linkedDbs;
    GetLinkedDbs(inDB, inID, linkedDbs);

    if (not linkedDbs.empty())
    {
        vector<el::object> linked;
        for (string& db : linkedDbs)
            linked.push_back(el::object(db));
        inHit["links"] = el::object(linked);
    }
}

void M6Server::init_scope(el::scope& scope)
{
    webapp::init_scope(scope);

    if (not mBaseURL.empty())
        scope.put("baseUrl", el::object(mBaseURL));

    vector<el::object> databanks;
    for (M6LoadedDatabank& db : mLoadedDatabanks)
    {
        el::object databank;
        databank["id"] = db.mID;
        databank["name"] = db.mName;
        databanks.push_back(databank);

        for (string alias : db.mAliases) {

            el::object databank;
            databank["id"] = alias;
            databank["name"] = alias;

            bool contains = false;
            for (el::object dbo : databanks) {
                if ( dbo["id"] == databank["id"] ) {
                    contains = true;
                    break;
                }
            }
            if ( !contains )
                databanks.push_back(databank);
        }
    }
    scope.put("databanks", el::object(databanks));

    scope.put("blastEnabled", el::object(not mBlastDatabanks.empty()));
    scope.put("alignEnabled", el::object(mAlignEnabled));
}

// --------------------------------------------------------------------

void M6Server::handle_request(const zh::request& req, zh::reply& rep)
{
    try
    {
        zh::webapp::handle_request(req, rep);

        LOG(DEBUG,"M6Server: completed %s request handling for %s", req.method.c_str(), req.uri.c_str());
    }
    catch (zh::status_type& s)
    {
        rep = zh::reply::stock_reply(s);
    }
    catch (std::exception& e)
    {
        LOG (ERROR, "M6Server: %s threw an error: %s", req.uri.c_str(), e.what());

        cerr << e.what() << endl;

        el::scope scope(req);
        init_scope(scope);

        scope.put("errormsg", el::object(e.what()));

        create_reply_from_template("error.html", scope, rep);
    }
}

void M6Server::handle_welcome(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    el::scope sub(scope);

    LOG(INFO,"handling welcome page");

    if (not mWebServices.empty())
    {
        el::object wsdl;

        const char* wss[] = { "mrsws_search", "mrsws_blast", "mrsws_align" };
        el::object wsc;

        for (const char* ws : wss)
        {
            if (zx::node* n = mConfig->find_first_node((boost::format("web-service[@service='%1%']/@location") % ws).str().c_str()))
                wsdl[ws] = mBaseURL + n->str() + "/wsdl";
        }
        sub.put("wsdl", wsdl);
    }

    create_reply_from_template("index.html", sub, reply);

    LOG(DEBUG,"done generating welcome page response");
}

void M6Server::handle_file(const zh::request& request,
    const el::scope& scope, zh::reply& reply)
{
    LOG(INFO, "handling file request for %s",
              scope["baseuri"].as<string>().c_str());

    fs::path file = get_docroot() / scope["baseuri"].as<string>();

    webapp::handle_file(request, scope, reply);

    if (file.extension() == ".html")
        reply.set_content_type("application/xhtml+xml");

    LOG(INFO, "Done generating file reply for %s",
              scope["baseuri"].as<string>().c_str());
}

void M6Server::handle_download(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    zh::parameter_map params;
    get_parameters(scope, params);

    string format = params.get("format", "entry").as<string>();
    string db = params.get("db", "").as<string>();

    string id;
    stringstream ss;
    uint32 n = 0;

    LOG(INFO, "download request recieved, format=%s, db=%s",
               format.c_str (), db.c_str ());

    for (auto& p : params)
    {
        if (p.first == "id")
        {
            id = p.second.as<string>();

            string m_db = db;
            size_t pos, pos2;
            if ((pos = id.find ("/")) != string::npos)
            {
                // databank name included in id
                m_db = id.substr (0, pos);

                if ((pos2 = id.find ("/", pos + 1)) != string::npos)

                    id = id.substr (pos + 1, pos2 - (pos + 1));
                else
                    id = id.substr (pos + 1);
            }

            ss << GetEntry (m_db, id, format);
            ++n;
        }
        else if (p.first == "nr")
        {
            M6Databank* databank = Load(db);
            if (databank == nullptr)
                THROW(("Databank %s not loaded", db.c_str()));


            ss << GetEntry(databank, format, boost::lexical_cast<uint32>(p.second.as<string>()));
            ++n;
        }
    }

    reply.set_content(ss.str(), "text/plain");

    if (n != 1 or id.empty())
        id = "mrs-data";

    reply.set_header("Content-disposition",
        (boost::format("attachment; filename=%1%.txt") % id).str());

    LOG(INFO, "done generating download reply format=%s, db=%s",
              format.c_str (), db.c_str ());
}

void M6Server::handle_entry(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    string db, nr, id;
    string q, rq, format;

    zh::parameter_map params;
    get_parameters(scope, params);

    db = params.get("db", "").as<string>();
    nr = params.get("nr", "").as<string>();
    id = params.get("id", "").as<string>();
    q = params.get("q", "").as<string>();
    rq = params.get("rq", "").as<string>();
    format = params.get("format", "entry").as<string>();

    LOG(INFO, "request entry format=%s db=%s id=%s nr=%s",
              format.c_str (), db.c_str (), id.c_str (), nr.c_str ());

    if (id.empty() and db.empty())
    {
        fs::path path(scope["baseuri"].as<string>());

        fs::path::iterator p = path.begin();
        if ((p++)->string() == "entry" and p != path.end())
        {
            db = (p++)->string();
            if (p != path.end())
                id = (p++)->string();
            if (p != path.end())
                format = (p++)->string();
            if (p != path.end())
                q = (p++)->string();
        }
    }

    if (db.empty() or (nr.empty() and id.empty()))        // shortcut
    {
        reply = zh::reply::redirect(mBaseURL);

        LOG(INFO, "redirecting empty entry request to base url");
        return;
    }

    M6Databank* mdb = Load(db);
    uint32 docNr = 0;

    if (mdb == nullptr and not id.empty())
    {
        for (string adb : UnAlias(db))
        {
            mdb = Load(adb);
            if (mdb != nullptr)
            {
                bool exists;
                tie(exists, docNr) = mdb->Exists("id", id);
                if (exists)
                {
                    db = adb;
                    nr = to_string(docNr);
                    break;
                }
            }
        }
    }

    if (mdb == nullptr)
        THROW(("Databank %s not loaded", db.c_str()));

    if (nr.empty())
    {
        bool exists;
        tie(exists, docNr) = mdb->Exists("id", id);
        if (not exists)
            THROW(("Entry %s does not exist in databank %s", id.c_str(), db.c_str()));
        if (docNr == 0)
            THROW(("Multiple entries with ID %s in databank %s", id.c_str(), db.c_str()));

    }
    else
    {
        docNr = boost::lexical_cast<uint32>(nr);
        unique_ptr<M6Document> doc(mdb->Fetch(docNr));
        if (doc)
            id = doc->GetAttribute("id");
    }

    unique_ptr<M6Document> document(mdb->Fetch(docNr));

    el::scope sub(scope);
    sub.put("db", el::object(db));
    sub.put("id", document->GetAttribute("id"));
    sub.put("nr", el::object(docNr));
    sub.put("text", document->GetText());
    sub.put("blastable", el::object(find_if(mBlastDatabanks.begin(), mBlastDatabanks.end(),
            [&db](M6BlastDatabank& bdb) -> bool { return bdb.mID == db; }) != mBlastDatabanks.end()));

    vector<string> linkedDbs;
    GetLinkedDbs(db, id, linkedDbs);
    if (not linkedDbs.empty())
    {
        vector<el::object> linked;
        for (string& db : linkedDbs)
            linked.push_back(el::object(db));
        sub.put("links", el::object(linked));
    }

    if (not q.empty())
        sub.put("q", el::object(q));
    else if (not rq.empty())
    {
        q = rq;
        sub.put("redirect", el::object(rq));
        sub.put("q", el::object(rq));
    }
    sub.put("format", el::object(format));

    const zx::element* dbConfig = M6Config::GetEnabledDatabank(db);

    // first stuff some data into scope

    el::object databank;
    databank["id"] = db;
    if (zx::element* name = dbConfig->find_first("name"))
        databank["name"] = name->content();
    else
        databank["name"] = db;
    if (zx::element* info = dbConfig->find_first("info"))
        databank["url"] = info->content();

//#ifndef NO_BLAST
//    databank["blastable"] = mNoBlast.count(db) == 0 and mdb->GetBlastDbCount() > 0;
//#endif
    sub.put("databank", databank);
//    sub.put("title", document->GetAttribute("title"));

    fs::ifstream data(get_docroot() / "entry.html");
    zx::document doc;
    doc.set_preserve_cdata(true);
    doc.read(data);

    zx::element* root = doc.child();

    try
    {
        const zx::element* format = M6Config::GetFormat(dbConfig->get_attribute("format"));
        if (format != nullptr)
            sub["formatScript"] = format->get_attribute("script");
        else if (not dbConfig->get_attribute("stylesheet").empty())
            sub["formatXSLT"] = dbConfig->get_attribute("stylesheet");

        process_xml(root, sub, "/");

        if (format != nullptr)
        {
            zx::element_set links(format->find("link"));
            for (zx::element* link : links)
            {
                try
                {
                    string ldb = link->get_attribute("db");
                    string id = link->get_attribute("id");
                    string ix = link->get_attribute("ix");
                    string anchor = link->get_attribute("an");
                    if (ldb.empty())
                        ldb = db;
                    if (id.empty())
                        continue;
                    boost::regex re(link->get_attribute("rx"));
                    create_link_tags(root, re, ldb, ix, id, anchor);
                }
                catch (...) {}
            }
        }

        if (not q.empty())
        {
            try
            {
                vector<string> terms;
                AnalyseQuery(q, terms);
                if (not terms.empty())
                {
                    string pattern = ba::join(terms, "|");

                    if (uc::contains_han(pattern))
                        pattern = string("(") + pattern + ")";
                    else
                        pattern = string("\\b(") + pattern + ")\\b";

                    boost::regex re(pattern, boost::regex_constants::icase);
                    highlight_query_terms(root, re);
                }
            }
            catch (...) {}
        }

        reply.set_content(doc);
    }
    catch (M6Redirect& redirect)
    {
        create_redirect(redirect.db, redirect.nr, "", false, request, reply);
    }

    LOG(INFO, "done generating reply for entry format=%s db=%s id=%s nr=%s",
              format.c_str (), db.c_str (), id.c_str (), nr.c_str ());
}

void M6Server::highlight_query_terms(zx::element* node, boost::regex& expr)
{
    for (zx::element* e : *node)
    {
        // do not highlight script, style or head blocks
        if (e->name() == "script" or e->name() == "head" or e->name() == "style")
            continue;

        highlight_query_terms(e, expr);
    }

    for (zx::node* n : node->nodes())
    {
        zx::text* text = dynamic_cast<zx::text*>(n);

        if (text == nullptr)
            continue;

        for (;;)
        {
            boost::smatch m;

            // somehow boost::regex_search works incorrectly with a const std::string...
#if defined(_MSC_VER)
            string s = text->str();
#else
            const string& s = text->str();
#endif
            if (not boost::regex_search(s, m, expr) or not m[0].matched or m[0].length() == 0)
                break;

            // split the text
            node->insert(text, new zx::text(m.prefix()));

            zx::element* span = new zx::element("span");
            span->add_text(m[0]);
            span->set_attribute("class", "highlight");
            node->insert(text, span);

            text->str(m.suffix());
        }
    }
}

void M6Server::create_link_tags(zx::element* node, boost::regex& expr,
    const string& inDatabank, const string& inIndex, const string& inID, const string& inAnchor)
{
    for (zx::element* e : *node)
        create_link_tags(e, expr, inDatabank, inIndex, inID, inAnchor);

    for (zx::node* n : node->nodes())
    {
        zx::text* text = dynamic_cast<zx::text*>(n);

        if (text == nullptr)
            continue;

        for (;;)
        {
            boost::smatch m;

            // somehow boost::regex_search works incorrectly with a const std::string...
            string s = text->str();

            if (s.length() > 1024 * 1024)    // if text is more than 1 MB we give up...
                break;

            if (not boost::regex_search(s, m, expr) or not m[0].matched or m[0].length() == 0)
                break;

            string db = inDatabank; if (ba::starts_with(db, "$")) db = m[atoi(db.c_str() + 1)];
            string id = inID;        if (ba::starts_with(id, "$")) id = m[atoi(id.c_str() + 1)];
            string ix = inIndex;    if (ba::starts_with(ix, "$")) ix = m[atoi(ix.c_str() + 1)];
//            string ix = node->get_attribute("index");            process_el(scope, ix);
            string an = inAnchor;    if (ba::starts_with(an, "$")) an = m[atoi(an.c_str() + 1)];
//            string title = node->get_attribute("title");        process_el(scope, title);
            string title;
            string q;
//            string q = node->get_attribute("q");                process_el(scope, q);

            bool exists = false;
            uint32 docNr = 0;

            zx::node* replacement = new zx::text(m[0]);

            try
            {
                M6Databank* mdb = Load(db);
                if (mdb != nullptr)
                {
                    M6Tokenizer::CaseFold(id);

                    tie(exists, docNr) = mdb->Exists(ix, id);

                    unique_ptr<zx::element> a(new zeep::xml::element("a"));

                    if (docNr != 0)
                        a->set_attribute("href",
                            (boost::format("entry?db=%1%&nr=%2%%3%%4%")
                                % zeep::http::encode_url(db)
                                % docNr
                                % (q.empty() ? "" : ("&q=" + zeep::http::encode_url(q)).c_str())
                                % (an.empty() ? "" : (string("#") + zeep::http::encode_url(an)).c_str())
                            ).str());
                    else
                    {
                        a->set_attribute("href",
                            (boost::format("link?db=%1%&ix=%2%&id=%3%%4%%5%")
                                % zeep::http::encode_url(db)
                                % zeep::http::encode_url(ix)
                                % zeep::http::encode_url(id)
                                % (q.empty() ? "" : ("&q=" + zeep::http::encode_url(q)).c_str())
                                % (an.empty() ? "" : (string("#") + zeep::http::encode_url(an)).c_str())
                            ).str());

                        if (not exists)
                            a->set_attribute("class", "not-found");
                    }

                    if (not title.empty())
                        a->set_attribute("title", title);

                    a->append(replacement);
                    replacement = a.release();
                }
            }
            catch (...) {}

            // split the text
            node->insert(text, new zx::text(m.prefix()));
            node->insert(text, replacement);
            text->str(m.suffix());
        }
    }
}

void M6Server::handle_search(const zh::request& request,
    const el::scope& scope, zh::reply& reply)
{
    string q, db, id, firstDb;
    uint32 page, hitCount = 0, firstDocNr = 0,
           nDBsSearched = 0;

    zh::parameter_map params;
    get_parameters(scope, params);

    q = params.get("q", "").as<string>();
    if (q.empty())
        q = params.get("query", "").as<string>();    // being backward compatible
    db = params.get("db", "").as<string>();
    page = params.get("page", 1).as<uint32>();

    if (page < 1)
        page = 1;

    el::scope sub(scope);
    sub.put("page", el::object(page));
    sub.put("db", el::object(db));
    sub.put("q", el::object(q));

    std::vector<M6LoadedDatabank>::iterator nameMatchingDBs=
        std::find_if(mLoadedDatabanks.begin(),mLoadedDatabanks.end(),
        [&db](M6LoadedDatabank&d) -> bool { return d.mID == db; } );

    bool dbIDMatched = nameMatchingDBs != mLoadedDatabanks.end() ;

    LOG(INFO, "handling search request for db=\'%s\' q=\'%s\'",
              db.c_str(), q.c_str());

    if (db.empty() or q.empty() or (db == "all" and q == "*"))
    {
        handle_welcome(request, scope, reply);
    }
    else if ( !dbIDMatched ) // db id not found, try aliases
    {
        uint32 hits_per_page = params.get("count", 3).as<uint32>();
        if (hits_per_page > 5)
            hits_per_page = 5;

//        sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
        sub.put("count", el::object(hits_per_page));
        sub.put("show", el::object(hits_per_page));

        string hitDb = db;
        bool ranked = false;

        boost::thread_group thr;
        boost::mutex m;
        vector<el::object> databanks;
        string error;

        std::vector<M6LoadedDatabank> searchDatabanks;
        if (db == "all")
            searchDatabanks.assign(mLoadedDatabanks.begin(),mLoadedDatabanks.end());
        else {
            for (M6LoadedDatabank& d : mLoadedDatabanks) {

                if (d.mAliases.count( db ) > 0)
                    searchDatabanks.push_back( d );
            }
        }

        for (M6LoadedDatabank& db : searchDatabanks)
        {
            thr.create_thread([&]() {
                try
                {
                    vector<el::object> hits;
                    uint32 c;
                    bool r;
                    string dbError;

                    Find(db.mID, q, true, 0, 5, false, hits, c, r, dbError);
                    nDBsSearched ++;

                    boost::mutex::scoped_lock lock(m);

                    if (not dbError.empty())
                        error = dbError;

                    hitCount += c;
                    ranked = ranked or r;

                    if (not hits.empty())
                    {
                        if (hitCount == c)
                        {
                            firstDb = db.mID;
                            firstDocNr = hits.front()["docNr"].as<uint32>();
                        }

                        el::object databank;
                        databank["id"] = db.mID;
                        databank["name"] = db.mName;
                        databank["hits"] = hits;
                        databank["hitCount"] = c;
                        databanks.push_back(databank);
                    }
                }
                catch (...) { }
            });
        }
        thr.join_all();

        if (not error.empty())
            sub.put("error", error);

        if (not databanks.empty())
        {
            sub.put("ranked", el::object(ranked));
            sub.put("hit-databanks", el::object(databanks));
        }
    }
    else // given id matches 1 database
    {
        uint32 hits_per_page = params.get("show", 15).as<uint32>();
        if (hits_per_page > 100)
            hits_per_page = 100;

        sub.put("page", el::object(page));
        sub.put("db", el::object(db));
//        sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
        sub.put("q", el::object(q));
        sub.put("show", el::object(hits_per_page));

        int32 maxresultcount = hits_per_page, resultoffset = 0;

        if (page > 1)
            resultoffset = (page - 1) * maxresultcount;

        vector<el::object> hits;
        bool ranked;
        string error;

        Find(db, q, true, resultoffset, maxresultcount, true, hits, hitCount, ranked, error);
        if (hitCount == 0)
        {
            sub.put("relaxed", el::object(true));
            Find(db, q, false, resultoffset, maxresultcount, true, hits, hitCount, ranked, error);
        }
        nDBsSearched ++;

        if (not hits.empty())
        {
            sub.put("hits", el::object(hits));
            if (hitCount == 1)
            {
                firstDb = db;
                firstDocNr = hits.front()["docNr"].as<uint32>();
            }
        }

        sub.put("first", el::object(resultoffset + 1));
        sub.put("last", el::object((uint64)(resultoffset + hits.size())));
        sub.put("hitCount", el::object(hitCount));
        sub.put("lastPage", el::object(((hitCount - 1) / hits_per_page) + 1));
        sub.put("ranked", ranked);
        sub.put("error", error);
    }

    vector<string> terms;
    AnalyseQuery(q, terms);
    if (not terms.empty())
    {
        M6Tokenizer::CaseFold(q);

        // add some spelling suggestions
        sort(terms.begin(), terms.end());
        terms.erase(unique(terms.begin(), terms.end()), terms.end());

        vector<el::object> suggestions;
        for (string& term : terms)
        {
            try
            {
                boost::regex re(string("\\b") + term + "\\b");

                vector<pair<string,uint16>> s;
                SpellCheck(db, term, s);
                if (s.empty())
                    continue;

                if (s.size() > 10)
                    s.erase(s.begin() + 10, s.end());

                if (s.size() > 10)
                    s.erase(s.begin() + 10, s.end());

                vector<el::object> alternatives;
                for (auto c : s)
                {
                    el::object alt;
                    alt["term"] = c.first;

                    // construct new query, with the term replaced by the alternative
                    ostringstream t;
                    ostream_iterator<char, char> oi(t);
                    boost::regex_replace(oi, q.begin(), q.end(), re, c.first,
                        boost::match_default | boost::format_all);

//                    if (Count(db, t.str()) > 0)
//                    {
                        alt["q"] = t.str();
                        alternatives.push_back(alt);
//                    }
                }

                if (alternatives.empty())
                    continue;

                el::object so;
                so["term"] = term;
                so["alternatives"] = alternatives;

                suggestions.push_back(so);
            }
            catch (...) {}    // silently ignore errors
        }

        if (not suggestions.empty())
            sub.put("suggestions", el::object(suggestions));
    }

    // OK, now if we only have one hit, we might as well show it directly of course...
    if (hitCount == 1)

        create_redirect(firstDb, firstDocNr, q, true, request, reply);

    else if (dbIDMatched)

        create_reply_from_template ("results.html", sub, reply);
    else
        create_reply_from_template ("results-for-all.html", sub, reply);

    LOG(INFO, "done generating reply for search db=\'%s\' q=\'%s\'",
              db.c_str(),q.c_str());
}

void M6Server::handle_link(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    string id, db, ix, q;

    zeep::http::parameter_map params;
    get_parameters(scope, params);

    id = params.get("id", "").as<string>();
    db = params.get("db", "").as<string>();
    ix = params.get("ix", "").as<string>();        if (ix == "full-text") ix = "*";
    q = params.get("q", "").as<string>();

    LOG(INFO, "handling link request for id=%s, db=%s, ix=%s, q=%s",
              id.c_str(), db.c_str(), ix.c_str(), q.c_str());

    M6Tokenizer::CaseFold(db);
    M6Tokenizer::CaseFold(id);

    create_redirect(db, ix, id, q, false, request, reply);

    LOG(INFO, "done generating link reply for id=%s, db=%s, ix=%s, q=%s",
              id.c_str(), db.c_str(), ix.c_str(), q.c_str());
}

void M6Server::handle_linked(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    string sdb, ddb;
    uint32 page, nr, hits_per_page = 15;

    zeep::http::parameter_map params;
    get_parameters(scope, params);

    sdb = params.get("s", "").as<string>();
    ddb = params.get("d", "").as<string>();
    nr = params.get("nr", "0").as<uint32>();
    page = params.get("page", 1).as<uint32>();

    LOG(INFO, "handling linked request for s=%s, d=%s, nr=%d, page=%d",
              sdb.c_str(), ddb.c_str(), nr, page);

    if (page < 1)
        page = 1;

    int32 maxresultcount = hits_per_page, resultoffset = 0;

    if (page > 1)
        resultoffset = (page - 1) * maxresultcount;

    el::scope sub(scope);
    sub.put("page", el::object(page));
    sub.put("db", el::object(ddb));

    if (nr == 0 or sdb.empty() or ddb.empty())
        THROW(("Invalid linked reference"));

    M6Databank* msdb = Load(sdb);
    M6Databank* mddb = Load(ddb);

    if (msdb == nullptr or mddb == nullptr)
        THROW(("Invalid databanks"));

    unique_ptr<M6Document> doc(msdb->Fetch(nr));
    if (not doc)
        THROW(("Document not found"));

    string id = doc->GetAttribute("id");

    el::object linkedInfo;
    linkedInfo["db"] = sdb;
    linkedInfo["id"] = id;
    sub.put("linked", linkedInfo);

    string q = string("[") + sdb + '/' + id + ']';
    sub.put("q", el::object(q));

    // Collect the links
    unique_ptr<M6Iterator> iter(mddb->GetLinkedDocuments(sdb, id));

    uint32 docNr, count = iter->GetCount();
    float score;

    nr = 1;
    while (resultoffset-- > 0 and iter->Next(docNr, score))
        ++nr;

    vector<el::object> hits;
    sub.put("first", el::object(nr));

    while (maxresultcount-- > 0 and iter->Next(docNr, score))
    {
        el::object hit;

        unique_ptr<M6Document> doc(mddb->Fetch(docNr));

        hit["nr"] = nr;
        hit["docNr"] = docNr;
        hit["id"] = doc->GetAttribute("id");
        hit["title"] = doc->GetAttribute("title");;

        AddLinks(sdb, doc->GetAttribute("id"), hit);

        hits.push_back(hit);

        ++nr;
    }

    if (maxresultcount > 0 and count + 1 > nr)
        count = nr - 1;

    if (count == 1)
        create_redirect(ddb, docNr, q, true, request, reply);
    else
    {
        sub.put("hits", el::object(hits));
        sub.put("hitCount", el::object(count));
        sub.put("lastPage", el::object(((count - 1) / hits_per_page) + 1));
        sub.put("last", el::object(nr - 1));
        sub.put("ranked", el::object(false));

        create_reply_from_template("results.html", sub, reply);
    }

    LOG(INFO, "done generating linked response for s=%s, d=%s, nr=%d, page=%d",
              sdb.c_str(), ddb.c_str(), nr, page);
}

void M6Server::handle_similar(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    string db, id, nr;
    uint32 page, hits_per_page = 15;

    zeep::http::parameter_map params;
    get_parameters(scope, params);

    db = params.get("db", "").as<string>();
    nr = params.get("nr", "").as<string>();
    page = params.get("page", 1).as<uint32>();

    LOG(INFO, "handling similar request for db=%s, nr=%s, page=%d",
              db.c_str(), nr.c_str(), page);

    int32 maxresultcount = hits_per_page, resultoffset = 0;

    if (page > 1)
        resultoffset = (page - 1) * maxresultcount;

    el::scope sub(scope);
    sub.put("page", el::object(page));
    sub.put("db", el::object(db));
//    sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
    sub.put("similar", el::object(nr));

    M6Databank* mdb = Load(db);
    if (mdb == nullptr) THROW(("Databank %s not loaded", db.c_str()));

    vector<string> queryTerms;

    unique_ptr<M6Document> doc(mdb->Fetch(boost::lexical_cast<uint32>(nr)));
    if (not doc)
        THROW(("Unable to fetch document"));

    M6Builder::IndexDocument(db, mdb, doc->GetText(), doc->GetAttribute("filename"), queryTerms);

    M6Iterator* filter = nullptr;
    unique_ptr<M6Iterator> results(mdb->Find(queryTerms, filter, false, resultoffset + maxresultcount));
    delete filter;

    if (results)
    {
        uint32 docNr, nr = 1, count = results->GetCount();
        float score;

        while (resultoffset-- > 0 and results->Next(docNr, score))
            ++nr;

        vector<el::object> hits;
        sub.put("first", el::object(nr));

        while (maxresultcount-- > 0 and results->Next(docNr, score))
        {
            el::object hit;

            unique_ptr<M6Document> doc(mdb->Fetch(docNr));

            score *= 100;
            if (score > 100)
                score = 100;

            hit["nr"] = nr;
            hit["docNr"] = docNr;
            hit["id"] = doc->GetAttribute("id");
            hit["title"] = doc->GetAttribute("title");;
            hit["score"] = trunc(score);

            AddLinks(db, doc->GetAttribute("id"), hit);

            hits.push_back(hit);

            ++nr;
        }

        if (maxresultcount > 0 and count + 1 > nr)
            count = nr - 1;

        sub.put("hits", el::object(hits));
        sub.put("hitCount", el::object(count));
        sub.put("lastPage", el::object(((count - 1) / hits_per_page) + 1));
        sub.put("last", el::object(nr - 1));
        sub.put("ranked", el::object(true));
    }

    create_reply_from_template("results.html", sub, reply);

    LOG(INFO, "done generating similar reply for db=%s, nr=%s, page=%d",
              db.c_str(), nr.c_str(), page);
}

void M6Server::handle_search_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    zh::parameter_map params;
    get_parameters(scope, params);

    string q = params.get("q", "").as<string>();
    string db = params.get("db", "").as<string>();
    uint32 offset = params.get("offset", 0).as<uint32>();
    uint32 count = params.get("count", 0).as<uint32>();

    LOG(INFO, "handling ajax search request for q=%s, db=%s, offset=%d, count=%d",
              q.c_str(), db.c_str(), offset, count);

    if (count <= 0)
        count = 5;

    vector<el::object> hits;

    uint32 hitCount;
    bool ranked;
    string error;

    Find(db, q, true, offset, count, true, hits, hitCount, ranked, error);
    if (hitCount == 0)
        Find(db, q, false, offset, count, true, hits, hitCount, ranked, error);

    el::object result;
    if (not hits.empty())
        result["hits"] = hits;

    result["ranked"] = ranked;
    result["error"] = error;

    reply.set_content(result.toJSON(), "text/javascript");

    LOG(INFO, "done generating ajax search response for q=%s, db=%s, offset=%d, count=%d",
              q.c_str(), db.c_str(), offset, count);
}

void M6Server::ProcessNewConfig(const string& inPage, zeep::http::parameter_map& inParams)
{
    typedef zh::parameter_map::iterator iter;
    typedef pair<iter,iter> range;

    unique_ptr<M6Config::File> config(new M6Config::File(*mConfigCopy));

    string btn = inParams.get("btn", "").as<string>();

    if (inPage == "global")
    {
        const char* dirs[] = { "mrs", "raw", "parser", "docroot", "blast" };
        for (const char* dir : dirs)
        {
            fs::path p = inParams.get(dir, "").as<string>();
            if (not fs::is_directory(p))
                THROW(("%s directory does not exist", dir));
            config->GetDirectory(dir)->content(p.string());
        }

        const char* tools[] = { "clustalo", "rsync" };
        for (const char* tool : tools)
        {
            fs::path p = inParams.get(tool, "").as<string>();
            if (not p.empty() and not fs::exists(p))
                THROW(("The %s tool does not exist", tool));
            config->GetTool(tool)->content(p.string());
        }
    }
    else if (inPage == "server")
    {
        zx::element* server = config->GetServer();
        server->set_attribute("addr", inParams.get("addr", "").as<string>());
        server->set_attribute("port", inParams.get("port", "").as<string>());
        server->set_attribute("log-forwarded", inParams.get("log_forwarded", false).as<bool>() ? "true" : "false");

        zx::element* e = server->find_first("base-url");
        string s = inParams.get("baseurl", "").as<string>();

        if (s.empty())
        {
            if (e != nullptr)
                server->remove(e);
        }
        else
        {
            if (e == nullptr)
            {
                e = new zx::element("base-url");
                server->append(e);
            }
            e->content(s);
        }

        const char* wss[] = { "mrsws_search", "mrsws_blast", "mrsws_align" };
        for (const char* ws : wss)
        {
            e = server->find_first((boost::format("web-service[@service='%1%']") % ws).str());
            s = inParams.get(ws, "").as<string>();

            if (s.empty())
            {
                if (e != nullptr)
                    server->remove(e);
            }
            else
            {
                if (e == nullptr)
                {
                    e = new zx::element("web-service");
                    e->set_attribute("service", ws);
                    e->set_attribute("ns", string("http://mrs.cmbi.ru.nl/mrsws/") + ws);
                    server->append(e);
                }
                e->set_attribute("location", s);
            }
        }

        server->set_attribute("addr", inParams.get("addr", "").as<string>());
    }
    else if (inPage == "parsers")
    {
//        if (btn == "delete")
//        {
//            string parserID = inParams.get("selected", "").as<string>();
//            zx::element* parser = config->GetParser(parserID);
//            if (parser != nullptr)
//            {
//                parser->parent()->remove(parser);
//                delete parser;
//            }
//        }
//        else if (btn == "add")
//        {
//
//        }
//        else
//        {
//
//        }
    }
    else if (inPage == "formats")
    {
        if (btn == "delete")
        {
            string formatID = inParams.get("selected", "").as<string>();
            zx::element* fmt = config->GetFormat(formatID);
            if (fmt != nullptr)
            {
                fmt->parent()->remove(fmt);
                delete fmt;
            }
        }
        else if (btn == "add")
        {
            config->CreateFormat();
        }
        else
        {
            string formatID = inParams.get("original-id", "").as<string>();
            zx::element* fmt = config->GetFormat(formatID);
            if (fmt == nullptr)
                THROW(("Unknown format %s", formatID.c_str()));

            string id = inParams.get("id", "").as<string>();
            if (id != formatID)
                fmt->set_attribute("id", id);

            string script = inParams.get("script", "").as<string>();
            fmt->set_attribute("script", script);

            range r[5] = {
                inParams.equal_range("rx"),
                inParams.equal_range("db"),
                inParams.equal_range("id"),
                inParams.equal_range("ix"),
                inParams.equal_range("an")
            };

            for_each(r, boost::end(r), [](range& ri) {
                if (ri.first == ri.second) THROW(("invalid data"));
                --ri.second;
            });

            zx::container::iterator l = fmt->begin();

            while (r[0].first != r[0].second)
            {
                if (l == fmt->end())
                    l = fmt->insert(l, new zx::element("link"));

                zx::element* link = *l;
                ++l;

                for_each(r, boost::end(r), [link](range& ri) {
                    string v = ri.first->second.as<string>();
                    if (v.empty())
                        link->remove_attribute(ri.first->first);
                    else
                        link->set_attribute(ri.first->first, v);
                    ++ri.first;
                });
            }

            if (l != fmt->end())
                fmt->erase(l, fmt->end());
        }
    }
    else if (inPage == "databanks")
    {
        if (btn == "delete")
        {
            string dbID = inParams.get("selected", "").as<string>();
            zx::element* db = config->GetConfiguredDatabank(dbID);

            db->parent()->remove(db);
            delete db;
        }
        else if (btn == "add")
        {
            config->CreateDatabank();
        }
        else
        {
            string dbID = inParams.get("original-id", "").as<string>();
            zx::element* db = config->GetConfiguredDatabank(dbID);

            string id = inParams.get("id", "").as<string>();
            if (id != dbID)
                db->set_attribute("id", id);

            db->set_attribute("enabled", inParams.get("enabled", false).as<bool>() ? "true" : "false");
            db->set_attribute("parser", inParams.get("parser", "").as<string>());
            db->set_attribute("fasta", inParams.get("fasta", false).as<bool>() ? "true" : "false");
            db->set_attribute("update", inParams.get("update", "never").as<string>());

            string format = inParams.get("format", "none").as<string>();
            if (format == "none")
                db->remove_attribute("format");
            else if (format == "xml")
            {
                db->remove_attribute("format");
                db->set_attribute("stylesheet", inParams.get("stylesheet", "").as<string>());
            }
            else
                db->set_attribute("format", format);

            range r[] = {
                inParams.equal_range("alias-id"),
                inParams.equal_range("alias-name")
            };

            for_each(r, boost::end(r), [](range& ri) {
                if (ri.first == ri.second) THROW(("invalid data"));
                --ri.second;
            });

            zx::element* a = db->find_first("aliases");
            if (r[0].first == r[0].second)        // no aliases
            {
                if (a != nullptr)
                {
                    db->remove(a);
                    delete a;
                }
            }
            else
            {
                if (a == nullptr)
                {
                    a = new zx::element("aliases");
                    db->append(a);
                }

                zx::container::iterator ai = a->begin();

                while (r[0].first != r[0].second)
                {
                    string id = r[0].first->second.as<string>();
                    ++r[0].first;
                    string name = r[1].first->second.as<string>();
                    ++r[1].first;

                    if (id.empty())
                        continue;

                    if (ai == a->end())
                        ai = a->insert(ai, new zx::element("alias"));

                    zx::element* alias = *ai;
                    ++ai;

                    alias->content(id);
                    if (name.empty())
                        alias->remove_attribute("name");
                    else
                        alias->set_attribute("name", name);
                }

                if (ai != a->end())
                    a->erase(ai, a->end());
            }

            const char* fields[] = { "name", "info", "filter", "source" };
            for (const char* field : fields)
            {
                string s = inParams.get(field, "").as<string>();
                zx::element* e = db->find_first(field);
                if (s.empty())
                {
                    if (e != nullptr)
                        db->remove(e);
                    delete e;
                }
                else
                {
                    if (e == nullptr)
                        db->append(e = new zx::element(field));
                    e->content(s);
                }
            }

            zx::element* source = db->find_first("source");
            string fetch = inParams.get("fetch", "").as<string>();
            string port = inParams.get("port", "").as<string>();

            if (not fetch.empty() and source == nullptr)
                THROW(("invalid: fetch contains text but source is empty"));

            if (fetch.empty())
                source->remove_attribute("fetch");
            else
                source->set_attribute("fetch", fetch);

            if (inParams.get("delete", false).as<bool>())
                source->set_attribute("delete", "true");
            else
                source->remove_attribute("delete");

            if (inParams.get("recursive", false).as<bool>())
                source->set_attribute("recursive", "true");
            else
                source->remove_attribute("recursive");

            if (port.empty())
                source->remove_attribute("port");
            else
                source->set_attribute("port", port);
        }
    }
    else if (inPage == "scheduler")
    {
        zx::element* schedule = config->GetSchedule();

        if (inParams.get("enabled", true).as<bool>())
            schedule->set_attribute("enabled", "true");
        else
            schedule->set_attribute("enabled", "false");

        string time = inParams.get("time", "").as<string>();

        if (time.empty())
            schedule->remove_attribute("time");
        else
        {
            vector<string> p;
            ba::split(p, time, ba::is_any_of(":"));

            if (p.size() != 2)
                THROW(("Invalid time"));

            uint32 hours = boost::lexical_cast<uint32>(p[0]);
            uint32 minutes = boost::lexical_cast<uint32>(p[1]);

            if (hours >= 24 or minutes > 59)
                THROW(("Invalid.time"));

            schedule->set_attribute("time", (boost::format("%02.2d:%02.2d") % hours % minutes).str());
        }

        string weekday = inParams.get("weekday", "friday").as<string>();
        const char* kWeekDays[] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
        if (find(kWeekDays, boost::end(kWeekDays), weekday) == boost::end(kWeekDays))
            THROW(("Invalid weekday"));

        schedule->set_attribute("weekday", weekday);
    }

    config->Validate();
    delete mConfigCopy;
    mConfigCopy = config.release();
}

void M6Server::handle_admin(const zh::request& request,
    const el::scope& scope, zh::reply& reply)
{
    if (mConfigCopy == nullptr)
        mConfigCopy = new M6Config::File();

    zeep::http::parameter_map params;
    get_parameters(scope, params);

    string submitted = params.get("submit", "").as<string>();
    if (not submitted.empty())
    {
        if (submitted == "restart")
        {
            mConfigCopy->WriteOut();

            M6Config::Reload();
            M6SignalCatcher::Signal(SIGHUP);

            el::scope sub(scope);
            zx::element* n;
            if ((n = mConfigCopy->GetServer()->find_first("base-url")) != nullptr)
                sub.put("baseurl", n->content());

            create_reply_from_template("restarting.html", sub, reply);
        }
        else
        {
            ProcessNewConfig(submitted, params);
            reply = zh::reply::redirect("admin");
        }
        return;
    }

    el::scope sub(scope);

    auto sortByID = [](const el::object& a, const el::object& b ) -> bool { return a["id"] < b["id"]; };

    // add the global settings
    el::object global, dirs, tools;

    dirs["mrs"] = mConfigCopy->GetDirectory("mrs")->content();
    dirs["raw"] = mConfigCopy->GetDirectory("raw")->content();
    dirs["blast"] = mConfigCopy->GetDirectory("blast")->content();
    dirs["docroot"] = mConfigCopy->GetDirectory("docroot")->content();
    dirs["parser"] = mConfigCopy->GetDirectory("parser")->content();
    global["dirs"] = dirs;

    tools["clustalo"] = mConfigCopy->GetTool("clustalo")->content();
    tools["rsync"] = mConfigCopy->GetTool("rsync")->content();
    global["tools"] = tools;

    sub.put("global", global);

    // add server settings
    const zx::element* serverConfig = mConfigCopy->GetServer();
    if (serverConfig != nullptr)
    {
        el::object server;

        server["addr"] = serverConfig->get_attribute("addr");
        server["port"] = serverConfig->get_attribute("port");
        server["log_forwarded"] = serverConfig->get_attribute("log-forwarded");

        zx::node* n;

        if ((n = serverConfig->find_first("base-url")) != nullptr)
            server["baseurl"] = n->str();

        const char* wss[] = { "mrsws_search", "mrsws_blast", "mrsws_align" };
        el::object wsc;

        for (const char* ws : wss)
        {
            if ((n = serverConfig->find_first_node((boost::format("web-service[@service='%1%']/@location") % ws).str().c_str())) != nullptr)
                wsc[ws] = n->str();
        }
        server["ws"] = wsc;

        sub.put("server", server);
    }

    // add parser settings
    vector<el::object> parsers;
    fs::path parserDir(mConfigCopy->GetDirectory("parser")->content());
    for (auto p = fs::directory_iterator(parserDir); p != fs::directory_iterator(); ++p)
    {
        fs::path path = p->path();

        if (path.extension().string() != ".pm" or path.filename().string() == "M6Script.pm")
            continue;

        el::object parser;
        parser["id"] = parser["script"] = path.filename().stem().string();
        parsers.push_back(parser);
    }

    for (const zx::element* e : mConfigCopy->GetParsers())
    {
        el::object parser;
        parser["id"] = e->get_attribute("id");
        parsers.push_back(parser);
    }

    sort(parsers.begin(), parsers.end(), sortByID);
    sub.put("parsers", parsers.begin(), parsers.end());

    // add formats
    vector<el::object> formats;
    for (const zx::element* e : mConfigCopy->GetFormats())
    {
        el::object format;
        format["id"] = e->get_attribute("id");
        format["script"] = e->get_attribute("script");

        vector<el::object> links;
        for (const zx::element* l : e->find("link"))
        {
            el::object link;
            link["nr"] = el::object((uint64)links.size() + 1);
            link["rx"] = l->get_attribute("rx");
            link["db"] = l->get_attribute("db");
            link["id"] = l->get_attribute("id");
            link["ix"] = l->get_attribute("ix");
            link["an"] = l->get_attribute("an");
            links.push_back(link);
        }

        if (not links.empty())
            format["links"] = el::object(links);

        formats.push_back(format);
    }
    sort(formats.begin(), formats.end(), sortByID);
    sub.put("formats", formats);

    vector<el::object> scripts;
    fs::path scriptDir(mConfigCopy->GetDirectory("docroot")->content());
    for (auto p = fs::directory_iterator(scriptDir / "formats"); p != fs::directory_iterator(); ++p)
    {
        fs::path path = p->path();

        if (path.extension().string() != ".js")
            continue;

        el::object script;
        script["id"] = script["script"] = path.filename().stem().string();
        scripts.push_back(script);
    }

    sort(parsers.begin(), parsers.end(), sortByID);
    sub.put("scripts", scripts.begin(), scripts.end());

    // add the databank settings
    vector<el::object> databanks;
    set<string> aliases;
    map<string,string> aliasnames;

    for (const zx::element* db : mConfigCopy->GetDatabanks())
    {
        zx::element* e;

        string id = db->get_attribute("id");
        string name = id;

        el::object databank;
        databank["id"] = id;
        if (db->get_attribute("stylesheet").empty())
            databank["format"] = db->get_attribute("format");
        else
        {
            databank["stylesheet"] = db->get_attribute("stylesheet");
            databank["format"] = "xml";
        }
        databank["parser"] = db->get_attribute("parser");
        databank["update"] = db->get_attribute("update");
        databank["enabled"] = db->get_attribute("enabled") == "true";
        databank["fasta"] = db->get_attribute("fasta") == "true";
        if ((e = db->find_first("filter")) != nullptr)
            databank["filter"] = e->content();
        if ((e = db->find_first("name")) != nullptr)
            databank["name"] = name = e->content();
        if ((e = db->find_first("info")) != nullptr)
            databank["info"] = e->content();
        if ((e = db->find_first("source")) != nullptr)
        {
            databank["source"] = e->content();

            el::object fetch;
            fetch["src"] = e->get_attribute("fetch");
            fetch["delete"] = e->get_attribute("delete");
            fetch["recursive"] = e->get_attribute("recursive");
            fetch["port"] = e->get_attribute("port");
            databank["fetch"] = fetch;
        }

        aliases.insert(id);
        aliasnames[id] = name;

        vector<el::object> dbaliases;
        for (const zx::element* a : db->find("aliases/alias"))
        {
            el::object alias;
            alias["name"] = alias["id"] = a->content();
            if (not a->get_attribute("name").empty())
                alias["name"] = a->get_attribute("name");
            dbaliases.push_back(alias);

            aliases.insert(a->content());
            aliasnames[a->content()] = alias["name"].as<string>();
        }

        databank["aliases"] = dbaliases;

        databanks.push_back(databank);
    }

    sort(databanks.begin(), databanks.end(), sortByID);
    sub.put("config-databanks", el::object(databanks));

    vector<el::object> aliasobjects;
    for (const string& alias : aliases)
    {
        el::object aliasobject;
        aliasobject["id"] = alias;
        aliasobject["name"] = aliasnames[alias].empty() ? alias : aliasnames[alias];
        aliasobjects.push_back(aliasobject);
    }
    sub.put("aliases", aliasobjects.begin(), aliasobjects.end());

    // add the scheduler settings
    el::object scheduler;
    zx::element* schedule = mConfigCopy->GetSchedule();
    scheduler["enabled"] = schedule->get_attribute("enabled") == "true";
    scheduler["time"] = schedule->get_attribute("time");
    scheduler["weekday"] = schedule->get_attribute("weekday");
    sub.put("scheduler", scheduler);

    create_reply_from_template("admin.html", sub, reply);
}

void M6Server::handle_admin_blast_queue_ajax(const zh::request& request,
    const el::scope& scope, zh::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    vector<el::object> jobs;

    for (const M6BlastJobDesc& jobDesc : M6BlastCache::Instance().GetJobList())
    {
        el::object job;
        job["id"] = jobDesc.id;
        job["db"] = jobDesc.db;
        job["queryLength"] = jobDesc.queryLength;
        job["status"] = jobDesc.status;
        jobs.push_back(job);
    }

    reply.set_content(el::object(jobs).toJSON(), "text/javascript");
}

void M6Server::handle_admin_blast_delete_ajax(const zh::request& request,
    const el::scope& scope, zh::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    string id = params.get("job", "").as<string>();

    M6BlastCache::Instance().DeleteJob(id);
    reply.set_content(el::object("ok").toJSON(), "text/javascript");
}

// --------------------------------------------------------------------
//    REST calls

void M6Server::handle_rest(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    reply = zh::reply::stock_reply(zh::not_found);

    fs::path path(scope["baseuri"].as<string>());
    fs::path::iterator p = path.begin();

    if ((p++)->string() == "rest")
    {
        string call = (p++)->string();

        if (call == "entry")
            handle_rest_entry(request, scope, reply);
        else if (call == "find")
            handle_rest_find(request, scope, reply);
    }
}

void M6Server::handle_rest_entry(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    string db, id;

    zh::parameter_map params;
    get_parameters(scope, params);

    string format = params.get("format", "entry").as<string>();

    fs::path path(scope["baseuri"].as<string>());
    fs::path::iterator p = path.begin();

    if ((p++)->string() == "rest" and
        (p++)->string() == "entry" and
        p != path.end())
    {
        db = (p++)->string();
        if (p != path.end())
            id = (p++)->string();
    }

    LOG(INFO, "handling rest entry request for id=%s, db=%s",
              id.c_str(), db.c_str());

    if (id.empty())
        THROW(("No id specified"));

    if (db.empty())
        THROW(("No db specified"));

    reply.set_content(GetEntry(db, id, format), "text/plain");

    LOG(INFO, "done generating rest entry response for id=%s, db=%s",
              id.c_str(), db.c_str());
}

void M6Server::handle_rest_find(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    string db, q;

    zh::parameter_map params;
    get_parameters(scope, params);

    uint32 resultoffset = params.get("offset", "0").as<uint32>();
    uint32 resultcount = params.get("count", "100").as<uint32>();

    fs::path path(scope["baseuri"].as<string>());
    fs::path::iterator p = path.begin();

    if ((p++)->string() == "rest" and
        (p++)->string() == "find" and
        p != path.end())
    {
        db = (p++)->string();
        if (p != path.end())
            q = (p++)->string();
    }

    LOG(INFO, "handling rest find request for q=%s, db=%s, offset=%d, count=%d",
              q.c_str(), db.c_str(), resultoffset, resultcount);

    if (q.empty())
        THROW(("No query specified"));

    if (db.empty())
        THROW(("No db specified"));

    vector<el::object> hits;
    uint32 hitCount;
    bool ranked;
    string error;

    Find(db, q, true, resultoffset, resultcount, false, hits, hitCount, ranked, error);

    if (not error.empty())
        reply.set_content(string("Error parsing query: ") + error, "text/plain");
    else if (hits.empty())
        reply.set_content("no hits found", "text/plain");
    else
    {
        ostringstream s;

        s << hitCount << " hits found, displaying " << hits.size() << " hits starting from " << resultoffset << endl;
        for (el::object& hit : hits)
        {
            s << hit["nr"] << '\t'
              << hit["id"] << '\t'
              << hit["score"] << '\t'
              << hit["title"] << endl;
        }

        reply.set_content(s.str(), "text/plain");
    }

    LOG(INFO, "done generating rest find response for q=%s, db=%s, offset=%d, count=%d",
              q.c_str(), db.c_str(), resultoffset, resultcount);
}

// --------------------------------------------------------------------

void M6Server::process_mrs_link(zx::element* node, const el::scope& scope, fs::path dir)
{
    string db = node->get_attribute("db");                process_el(scope, db);
    string nr = node->get_attribute("nr");                process_el(scope, nr);
    string id = node->get_attribute("id");                process_el(scope, id);
    string ix = node->get_attribute("index");            process_el(scope, ix);
    string an = node->get_attribute("anchor");            process_el(scope, an);
    string title = node->get_attribute("title");        process_el(scope, title);
    string q = node->get_attribute("q");                process_el(scope, q);

    bool exists = false;

    if (nr.empty())
    {
        try
        {
            M6Databank* mdb = Load(db);
            if (mdb != nullptr)
            {
                unique_ptr<M6Iterator> rset(mdb->Find(ix, id));

                uint32 docNr, docNr2; float rank;
                if (rset and rset->Next(docNr, rank))
                {
                    exists = true;
                    if (not rset->Next(docNr2, rank))
                        nr = to_string(docNr);
                }
            }
        }
        catch (...) {}
    }

    zx::element* a = new zx::element("a");

    if (not nr.empty())
        a->set_attribute("href",
            (boost::format("entry?db=%1%&nr=%2%%3%%4%")
                % zh::encode_url(db)
                % zh::encode_url(nr)
                % (q.empty() ? "" : ("&q=" + zh::encode_url(q)).c_str())
                % (an.empty() ? "" : (string("#") + zh::encode_url(an)).c_str())
            ).str());
    else
    {
        a->set_attribute("href",
            (boost::format("link?db=%1%&ix=%2%&id=%3%%4%%5%")
                % zh::encode_url(db)
                % zh::encode_url(ix)
                % zh::encode_url(id)
                % (q.empty() ? "" : ("&q=" + zh::encode_url(q)).c_str())
                % (an.empty() ? "" : (string("#") + zh::encode_url(an)).c_str())
            ).str());

        if (not exists)
            a->set_attribute("class", "not-found");
    }

    if (not title.empty())
        a->set_attribute("title", title);

    zx::container* parent = node->parent();
    assert(parent);
    parent->insert(node, a);

    for (zx::node* c : node->nodes())
    {
        zx::node* clone = c->clone();
        a->push_back(clone);
        process_xml(clone, scope, dir);
    }
}

void M6Server::process_mrs_enable(zx::element* node, const el::scope& scope, fs::path dir)
{
    string test = node->get_attribute("test");
    bool enabled = evaluate_el(scope, test);

    for (zx::node* c : node->nodes())
    {
        zx::node* clone = c->clone();
        zx::element* e = dynamic_cast<zx::element*>(clone);

        if (e != nullptr and (e->name() == "input" or e->name() == "option" or e->name() == "select"))
        {
            if (enabled)
                e->remove_attribute("disabled");
            else
                e->set_attribute("disabled", "disabled");
        }

        zx::container* parent = node->parent();
        assert(parent);

        parent->insert(node, clone);    // insert before processing, to assign namespaces
        process_xml(clone, scope, dir);
    }
}

// --------------------------------------------------------------------

void M6Server::create_redirect(const string& databank, const string& inIndex, const string& inValue,
    const string& q, bool redirectForQuery, const zh::request& request, zh::reply& reply)
{
    string host = request.local_address;
    for (const zh::header& h : request.headers)
    {
        if (ba::iequals(h.name, "Host"))
        {
            host = h.value;
            break;
        }
    }

    // for some weird reason, local_port is sometimes 0 (bug in asio?)
    if (request.local_port != 80 and request.local_port != 0 and host.find(':') == string::npos)
    {
        host += ':';
        host += to_string(request.local_port);
    }

    bool exists = false;
    M6Databank* mdb = Load(databank);

    if (mdb != nullptr)
    {
        uint32 docNr;
        tie(exists, docNr) = mdb->Exists(inIndex, inValue);
        if (exists)
        {
            string location;
            if (docNr != 0)
            {
                location =
                    (boost::format("http://%1%/entry?db=%2%&nr=%3%&%4%=%5%")
                        % host
                        % zh::encode_url(databank)
                        % docNr
                        % (redirectForQuery ? "rq" : "q")
                        % zh::encode_url(q)
                    ).str();
            }
            else
            {
                location =
                    (boost::format("http://%1%/search?db=%2%&q=%3%:\"%4%\"")
                        % host
                        % zh::encode_url(databank)
                        % inIndex
                        % zh::encode_url(inValue)
                    ).str();
            }

            reply = zh::reply::redirect(location);
        }
    }

    if (not exists)
    {
        // ouch, nothing found... fall back to a lame page that says so
        // (this is an error, actually)

        el::scope scope(request);
        create_reply_from_template("results.html", scope, reply);
    }
}

void M6Server::create_redirect(const string& databank, uint32 inDocNr,
    const string& q, bool redirectForQuery, const zh::request& request, zh::reply& reply)
{
    string host = request.local_address;
    for (const zh::header& h : request.headers)
    {
        if (ba::iequals(h.name, "Host"))
        {
            host = h.value;
            break;
        }
    }

    // for some weird reason, local_port is sometimes 0 (bug in asio?)
    if (request.local_port != 80 and request.local_port != 0 and host.find(':') == string::npos)
    {
        host += ':';
        host += to_string(request.local_port);
    }

    M6Databank* mdb = Load(databank);
    if (mdb == nullptr)
    {
        // ouch, nothing found... fall back to a lame page that says so
        // (this is an error, actually)

        el::scope scope(request);
        create_reply_from_template("results.html", scope, reply);
    }
    else
    {
        string location =
            (boost::format("http://%1%/entry?db=%2%&nr=%3%&%4%=%5%")
                % host
                % zh::encode_url(databank)
                % inDocNr
                % (redirectForQuery ? "rq" : "q")
                % zh::encode_url(q)
            ).str();

        reply = zh::reply::redirect(location);
    }
}

// --------------------------------------------------------------------

void M6Server::SpellCheck(const string& inDatabank, const string& inTerm,
    vector<pair<string,uint16>>& outCorrections)
{
    if (inDatabank == "all")
    {
        vector<pair<string,uint16>> corrections;

        for (M6LoadedDatabank& db : mLoadedDatabanks)
        {
            vector<pair<string,uint16>> s;
            SpellCheck(db.mID, inTerm, s);
            if (not s.empty())
                corrections.insert(corrections.end(), s.begin(), s.end());
        }

        sort(corrections.begin(), corrections.end(),
            [](const pair<string,uint16>& a, const pair<string,uint16>& b) -> bool { return a.second > b.second; });

        set<string> words;
        for (auto c : corrections)
        {
            if (words.count(c.first))
                continue;
            outCorrections.push_back(c);
            words.insert(c.first);
        }
    }
    else
    {
        M6Databank* db = Load(inDatabank);
        if (db != nullptr)
            db->SuggestCorrection(inTerm, outCorrections);
    }
}

// ====================================================================
// Blast

void M6Server::handle_blast(const zeep::http::request& request, const el::scope& scope, zeep::http::reply& reply)
{
    // default parameters
    string matrix = "BLOSUM62", expect = "10.0";
    int wordSize = 0, gapOpen = -1, gapExtend = -1, reportLimit = 250;
    bool filter = true, gapped = true;

    zeep::http::parameter_map params;
    get_parameters(scope, params);

    el::scope sub(scope);

    vector<el::object> blastdatabanks;
    for (M6BlastDatabank& ldb : mBlastDatabanks)
    {
        el::object databank;
        databank["id"] = ldb.mID;
        databank["name"] = ldb.mName;
        blastdatabanks.push_back(databank);
    }

    // fetch some parameters, if any
    string db = params.get("db", "sprot").as<string>();
    uint32 nr = params.get("nr", "0").as<uint32>();

    LOG(INFO, "handling blast request for db=%s, nr=%d",
              db.c_str(), nr);

//    string query = params.get("query", "").as<string>();
    string query;
    if (nr != 0 and not db.empty() and Load(db) != nullptr)
        query = GetEntry(Load(db), "fasta", nr);

    sub.put("blastdatabanks", el::object(blastdatabanks));
    sub.put("blastdb", db);
    sub.put("query", query);

    const char* expectRange[] = { "0.001", "0.01", "0.1", "1.0", "10.0", "100.0", "1000.0" };
    sub.put("expectRange", expectRange, boost::end(expectRange));
    sub.put("expect", expect);

    uint32 wordSizeRange[] = { 2, 3, 4 };
    sub.put("wordSizeRange", wordSizeRange, boost::end(wordSizeRange));
    sub.put("wordSize", wordSize);

    const char* matrices[] = { "BLOSUM45", "BLOSUM50", "BLOSUM62", "BLOSUM80", "BLOSUM90", "PAM30", "PAM70", "PAM250" };
    sub.put("matrices", matrices, boost::end(matrices));
    sub.put("matrix", matrix);

    int32 gapOpenRange[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
    sub.put("gapOpenRange", gapOpenRange, boost::end(gapOpenRange));
    sub.put("gapOpen", gapOpen);

    int32 gapExtendRange[] = { 1, 2, 3, 4 };
    sub.put("gapExtendRange", gapExtendRange, boost::end(gapExtendRange));
    sub.put("gapExtend", gapExtend);

    uint32 reportLimitRange[] = { 100, 250, 500, 1000 };
    sub.put("reportLimitRange", reportLimitRange, boost::end(reportLimitRange));
    sub.put("reportLimit", reportLimit);

    sub.put("filter", filter);
    sub.put("gapped", gapped);

    create_reply_from_template("blast.html", sub, reply);

    LOG(INFO, "done generating blast response for db=%s, nr=%d",
              db.c_str(), nr);
}

void M6Server::handle_blast_results_ajax(const zeep::http::request& request, const el::scope& scope, zeep::http::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    string id = params.get("job", "").as<string>();
    uint32 hitNr = params.get("hit", 0).as<uint32>();

    el::object result;

    M6BlastResultPtr job(M6BlastCache::Instance().JobResult(id));
    if (not job)
        result["error"] = "Job expired";
    else if (hitNr > 0)    // we need to return the hsps for this hit
    {
        const list<M6Blast::Hit>& hits(job->mHits);
        if (hitNr > hits.size())
            THROW(("Hitnr out of range"));

        list<M6Blast::Hit>::const_iterator hit = hits.begin();
        advance(hit, hitNr - 1);

        const list<M6Blast::Hsp>& hsps(hit->mHsps);

        vector<el::object> jhsps;

        for (const M6Blast::Hsp& hsp : hsps)
        {
            string queryAlignment = hsp.mQueryAlignment;

            // calculate the offsets for the graphical representation of this hsp
            uint32 qf = hsp.mQueryStart;
            uint32 qt = hsp.mQueryEnd;
            uint32 ql = job->mQueryLength;

            uint32 sf = hsp.mTargetStart;
            uint32 st = hsp.mTargetEnd;
            uint32 sl = hsp.mTargetLength;

            uint32 length = static_cast<uint32>(queryAlignment.length());
            uint32 before = qf;
            if (before < sf)
                before = sf;

            uint32 after = ql - qt;
            if (after < sl - st)
                after = sl - st;

            length += before + after;

            float factor = 150;
            factor /= length;

            uint32 ql1, ql2, ql3, ql4, sl1, sl2, sl3, sl4;

            if (qf < before)
                ql1 = uint32(factor * (before - qf));
            else
                ql1 = 0;
            ql2 = uint32(factor * qf);
            ql3 = uint32(factor * queryAlignment.length());
            ql4 = uint32(factor * (ql - qt));

            if (sf < before)
                sl1 = uint32(factor * (before - sf));
            else
                sl1 = 0;
            sl2 = uint32(factor * sf);
            sl3 = uint32(factor * queryAlignment.length());
            sl4 = uint32(factor * (sl - st));

            if (ql1 > 0 and ql1 + ql2 < sl2)
                ql1 = sl2 - ql2;

            if (sl1 > 0 and sl1 + sl2 < ql2)
                sl1 = ql2 - sl2;

            el::object h;
            h["nr"] = (uint64)jhsps.size() + 1;
            h["score"] = hsp.mScore;
            h["bitScore"] = hsp.mBitScore;
            h["expect"] = hsp.mExpect;
            h["queryAlignment"] = queryAlignment;
            h["queryStart"] = hsp.mQueryStart;
            h["subjectAlignment"] = hsp.mTargetAlignment;
            h["subjectStart"] = hsp.mTargetStart;
            h["midLine"] = hsp.mMidLine;
            h["identity"] = hsp.mIdentity;
            h["positive"] = hsp.mPositive;
            h["gaps"] = hsp.mGaps;

            vector<el::object> qls(4);
            qls[0] = ql1; qls[1] = ql2; qls[2] = ql3; qls[3] = ql4;
            h["ql"] = qls;

            vector<el::object> sls(4);
            sls[0] = sl1; sls[1] = sl2; sls[2] = sl3; sls[3] = sl4;
            h["sl"] = sls;

            jhsps.push_back(h);
        }

        result = jhsps;
    }
    else
    {
        const list<M6Blast::Hit>& hits(job->mHits);

        vector<el::object> jhits;

        for (const M6Blast::Hit& hit : hits)
        {
            const list<M6Blast::Hsp>& hsps(hit.mHsps);
            if (hsps.empty())
                continue;
            const M6Blast::Hsp& best = hsps.front();

            uint32 coverageStart = 0, coverageLength = 0, coverageColor = 1;
            float bin = (best.mPositive * 4.0f) / best.mQueryAlignment.length();

            if (bin >= 3.5)
                coverageColor = 1;
            else if (bin >= 3)
                coverageColor = 2;
            else if (bin >= 2.5)
                coverageColor = 3;
            else if (bin >= 2)
                coverageColor = 4;
            else
                coverageColor = 5;

            float queryLength = static_cast<float>(job->mQueryLength);
            const int kGraphicWidth = 100;

            coverageStart = uint32(best.mQueryStart * kGraphicWidth / queryLength);
            coverageLength = uint32(best.mQueryAlignment.length() * kGraphicWidth / queryLength);

            el::object h;
            h["nr"] = (uint64)jhits.size() + 1;
            h["db"] = hit.mDb;
            h["doc"] = hit.mID;
            h["seq"] = hit.mChain;
            h["desc"] = hit.mTitle;
            h["bitScore"] = best.mBitScore;
            h["expect"] = best.mExpect;
            h["hsps"] = (uint64)hsps.size();

            el::object coverage;
            coverage["start"] = coverageStart;
            coverage["length"] = coverageLength;
            coverage["color"] = coverageColor;
            h["coverage"] = coverage;

            jhits.push_back(h);
        }

        result = jhits;
    }

    reply.set_content(result.toJSON(), "text/javascript");
}

ostream& operator<<(ostream& os, M6BlastJobStatus status)
{
    os << StatusToString (status);
}

void M6Server::handle_blast_status_ajax(const zeep::http::request& request, const el::scope& scope, zeep::http::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    string ids = params.get("jobs", "").as<string>();
    vector<string> jobs;

    LOG (DEBUG, "handle_blast_status_ajax for jobs string of length %d", ids.size ());

    if (not ids.empty())
        ba::split(jobs, ids, ba::is_any_of(";"));

    vector<el::object> jjobs;

    for (const string& id : jobs)
    {
        try
        {
            M6BlastJobStatus status;
            string error;
            uint32 hitCount;
            double bestScore;

            tie(status, error, hitCount, bestScore) = M6BlastCache::Instance().JobStatus(id);

            el::object jjob;
            jjob["id"] = id;
            jjob["status"] = StatusToString (status);

            switch (status)
            {
                case bj_Finished:
                    jjob["hitCount"] = hitCount;
                    jjob["bestEValue"] = bestScore;
                    break;

                case bj_Error:
                    jjob["error"] = error;
                    break;

                default:
                    break;
            }

            jjobs.push_back(jjob);
        }
        catch (...)
        {
            LOG (ERROR, "handle_blast_status_ajax error: \"%s\"", id.c_str ());
        }
    }

    el::object json(jjobs);
    reply.set_content(json.toJSON(), "text/javascript");
}

void M6Server::handle_blast_submit_ajax(
    const zeep::http::request&    request,
    const el::scope&            scope,
    zeep::http::reply&            reply)
{
    // default parameters
    string id, db, matrix, expect, query, program;
    int wordSize = 0, gapOpen = -1, gapExtend = -1, reportLimit = 250;
    bool filter = true, gapped = true;

    zeep::http::parameter_map params;
    get_parameters(scope, params);

    // fetch the parameters
    id = params.get("id", "").as<string>();            // id is used by the client
    db = params.get("db", "pdb").as<string>();
    matrix = params.get("matrix", "BLOSUM62").as<string>();
    expect = params.get("expect", "10.0").as<string>();
    query = params.get("query", "").as<string>();
    program = params.get("program", "blastp").as<string>();

    wordSize = params.get("wordSize", wordSize).as<int>();
    gapped = params.get("gapped", true).as<bool>();
    gapOpen = params.get("gapOpen", gapOpen).as<int>();
    gapExtend = params.get("gapExtend", gapExtend).as<int>();
    reportLimit = params.get("reportLimit", reportLimit).as<int>();
    filter = params.get("filter", true).as<bool>();

    LOG(INFO, "handling ajax blast submit request for query=%s, db=%s",
              query.c_str(), db.c_str());

    // validate and unalias the databank
    bool found = false;
    for (M6BlastDatabank& bdb : mBlastDatabanks)
    {
        if (bdb.mID == db)
        {
            db = ba::join(bdb.mIDs, ";");
            found = true;
            break;
        }
    }

    if (not found)
        THROW(("Databank '%s' not configured", db.c_str()));

    // first parse the query (in case it is in FastA format)
    ba::replace_all(query, "\r\n", "\n");
    ba::replace_all(query, "\r", "\n");
    istringstream is(query);
    string qid;

    el::object result;
    result["clientId"] = id;

    try
    {
        query.clear();
        for (;;)
        {
            string line;
            getline(is, line);
            if (line.empty() and is.eof())
                break;

            if (qid.empty() and ba::starts_with(line, ">"))
            {
                qid = line.substr(1);
                continue;
            }

            ba::to_upper(line);
            query += line;
        }

            // validate the sequence
        if (program == "blastn" or program == "blastx" or program == "tblastx")
        {
            if (not ba::all(query, ba::is_any_of("ACGT")))
            {
                PRINT(("Error in parameters:\n%s", request.payload.c_str()));
                THROW(("not a valid sequence"));
            }
        }
        else
        {
            if (not ba::all(query, ba::is_any_of("LAGSVETKDPIRNQFYMHCWBZXU")))
            {
                PRINT(("Error in parameters:\n%s", request.payload.c_str()));
                THROW(("not a valid sequence"));
            }
        }

        string jobId = M6BlastCache::Instance().Submit(
            db, query, program, matrix, wordSize,
            boost::lexical_cast<double>(expect), filter,
            gapped, gapOpen, gapExtend, reportLimit);

        LOG (DEBUG, "handle_blast_submit_ajax: created new job with id: %s", jobId.c_str ());

        // and answer with the created job ID
        result["id"] = jobId;
        result["qid"] = qid;
        result["status"] = StatusToString (bj_Queued);
    }
    catch (exception& e)
    {
        result["status"] = StatusToString (bj_Error);
        result["error"] = e.what();
    }

    reply.set_content(result.toJSON(), "text/javascript");

    LOG(INFO, "done generating ajax blast submit response for query=%s, db=%s",
              query.c_str(), db.c_str());
}

void M6Server::handle_align(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    el::scope sub(scope);

    string seqstr = params.get("seqs", "").as<string>();

    LOG(INFO, "handle align request with seqs=%s", seqstr.c_str());

    ba::replace_all(seqstr, "\r\n", "\n");
    ba::replace_all(seqstr, "\r", "\n");

    map<string,shared_ptr<M6Parser>> parsers;

    if (not seqstr.empty())
    {
        vector<string> seqs;
        ba::split(seqs, seqstr, ba::is_any_of(";"));
        string fasta;

        for (string& ts : seqs)
        {
            string::size_type s = ts.find('/');
            if (s == string::npos)
                THROW(("Invalid parameters passed for align"));
            fasta += GetEntry(ts.substr(0, s), ts.substr(s + 1), "fasta");
        }

        sub.put("input", el::object(fasta));
    }

    create_reply_from_template("align.html", sub, reply);

    LOG(INFO, "done generating align response for seqs=%s", seqstr.c_str());
}

void M6Server::handle_align_submit_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    el::object result;

    string fasta = params.get("input", "").as<string>();

    LOG(INFO, "handle ajax align request with fasta=%s", fasta.c_str());

    if (fasta.empty())
        result["error"] = "No input specified for align";
    else
    {
        try
        {
            string clustalo = M6Config::GetTool("clustalo");
            if (clustalo.empty())
                clustalo = "/usr/bin/clustalo";

            vector<const char*> args;
            args.push_back(clustalo.c_str());
            args.push_back("-i");
            args.push_back("-");
            args.push_back(nullptr);

            double maxRunTime = M6Config::GetMaxRunTime("clustalo");
            if (maxRunTime == 0)
                maxRunTime = 30;
            stringstream in(fasta), out, err;

            if (ForkExec(args, maxRunTime, in, out, err) == 0)
            {
                result["alignment"] = out.str();
                if (not err.str().empty())
                    result["error"] = err.str();
            }
            else
            {
                string error_message = "Error running clustalo";
                error_message += err.str();

                result["error"] = error_message;
            }
        }
        catch (exception& e)
        {
            result["error"] = e.what();
        }
    }

    reply.set_content(result.toJSON(), "text/javascript");

    LOG(INFO, "done generating ajax align response for fasta=%s", fasta.c_str());
}

// --------------------------------------------------------------------

void M6Server::handle_status(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    el::scope sub(scope);

    LOG(INFO, "handling status request");

    vector<el::object> databanks;
//    for (M6LoadedDatabank& db : mLoadedDatabanks)

    for (zx::element* db : M6Config::GetDatabanks())
    {
        if (db->get_attribute("enabled") == "false")
            continue;

        string id = db->get_attribute("id");

        el::object databank;
        databank["id"] = id;
        if (zx::element* n = db->find_first("name"))
            databank["name"] = n->content();

        M6DatabankInfo info = {};
        M6Databank* dbo = Load(id);

        if (dbo != nullptr)
            dbo->GetInfo(info);

        databank["entries"] = info.mDocCount;
        databank["version"] = info.mVersion;
        databank["buildDate"] = info.mLastUpdate;
//        databank["size"] = info.mTotalSize;
        databank["size"] = info.mRawTextSize;

        databanks.push_back(databank);
    }
    sub.put("statusDatabanks", el::object(databanks));

    create_reply_from_template("status.html", sub, reply);
    reply.set_header("Cache-Control", "no-cache");

    LOG(INFO, "done generating status response");
}

void M6Server::handle_status_ajax(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    vector<el::object> databanks;
    vector<string> scheduled;

    LOG(INFO, "handling ajax status request");

    M6Scheduler::Instance().GetScheduledDatabanks(scheduled);

    for (zx::element* db : M6Config::GetDatabanks())
    {
        if (db->get_attribute("enabled") == "false")
            continue;

        string id = db->get_attribute("id");

        el::object databank;
        databank["id"] = id;

        bool dbScheduled = find(scheduled.begin(), scheduled.end(), id) != scheduled.end();

        string stage;
        float progress;
        if (M6Status::Instance().GetUpdateStatus(id, stage, progress))
        {
            el::object update;
            if (progress < 0 and dbScheduled)
                update["stage"] = "scheduled";
            else
            {
                update["progress"] = progress;
                update["stage"] = stage;
            }
            databank["update"] = update;
        }
        else if (dbScheduled)
        {
            el::object update;
            update["stage"] = "scheduled";
            databank["update"] = update;
        }

        databanks.push_back(databank);
    }

    reply.set_content(el::object(databanks).toJSON(), "text/javascript");
    reply.set_header("Cache-Control", "no-cache");

    LOG(INFO, "done generating ajax status response");
}

void M6Server::handle_info(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    el::scope sub(scope);

    string dbAlias = params.get("db", "").as<string>();

    LOG(INFO, "handling info request for db=%s", dbAlias.c_str());

    if (dbAlias.empty())
    {
        THROW(("No databank specified"));
    }

    LOG (DEBUG, "M6Server: info request is for databank %s", dbAlias.c_str ());

    vector<string> aliased = UnAlias(dbAlias);
    bool bAlias = aliased.size()>1 ||
                    aliased.size () == 1 && aliased[0] != dbAlias ;

    vector<el::object> databanks;
    for (string db : aliased)
    {
        LOG (DEBUG, "M6Server: Resolved %s to %s", dbAlias.c_str (), db.c_str ());

    for (M6LoadedDatabank& ldb : mLoadedDatabanks)
    {
        if (ldb.mID != db)
            continue;

        const zx::element* dbConfig = M6Config::GetEnabledDatabank(db);

        M6DatabankInfo info;
        ldb.mDatabank->GetInfo(info);

        el::object databank;
        databank["id"] = ldb.mID;
        databank["name"] = ldb.mName;
        databank["count"] = info.mDocCount;
        databank["version"] = info.mVersion;
        databank["path"] = info.mDbDirectory.string();
        databank["onDiskSize"] = info.mTotalSize;
        databank["rawDataSize"] = info.mRawTextSize;
        databank["buildDate"] = info.mLastUpdate;
        databank["parser"] = dbConfig->get_attribute("parser");
        if (zx::element* info = dbConfig->find_first("info"))
            databank["info"] = info->content();

        vector<el::object> indices;
        for (M6IndexInfo& iinfo : info.mIndexInfo)
        {
            el::object index;

            index["id"] = iinfo.mName;
            index["desc"] = iinfo.mDesc;

            switch (iinfo.mType)
            {
                case eM6CharIndex:            index["type"] = "unique string"; break;
                case eM6NumberIndex:        index["type"] = "unique number"; break;
                case eM6FloatIndex:         index["type"] = "unique fp number"; break;
//                case eM6DateIndex:            index["type"] = "date"; break;
                case eM6CharMultiIndex:        index["type"] = "string"; break;
                case eM6NumberMultiIndex:    index["type"] = "number"; break;
                case eM6FloatMultiIndex:    index["type"] = "floating point number"; break;
//                case eM6DateMultiIndex:        index["type"] = "date"; break;
                case eM6CharMultiIDLIndex:    index["type"] = "string"; break;
                case eM6CharWeightedIndex:    index["type"] = "full text"; break;
            }

            index["count"] = iinfo.mCount;
            index["size"] = iinfo.mFileSize;

            indices.push_back(index);
        }

        databank["indices"] = el::object(indices);

        databanks.push_back(databank);
        if (!bAlias)
            sub.put("databank", databank);
        break;
    }
    }

    LOG (DEBUG, "M6Server: creating info reply for %d databanks",
                aliased.size ());

    if(bAlias) {

        string dbList = "";
        for (string db : aliased) {
            if(! dbList.empty() )
                dbList += " + ";
            dbList += db ;
        }

        sub.put("info-databanks", el::object(databanks));
        sub.put("alias",dbAlias);
        sub.put("aliased",dbList);

        create_reply_from_template("info-alias.html", sub, reply);
    }
    else {
        create_reply_from_template("info.html", sub, reply);
    }

    LOG(INFO, "done generating info response for db=%s", dbAlias.c_str());
}

void M6Server::handle_browse(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
    zeep::http::parameter_map params;
    get_parameters(scope, params);

    el::scope sub(scope);

    string db = params.get("db", "").as<string>();
    if (db.empty())
        THROW(("No databank specified"));

    string ix = params.get("ix", "").as<string>();
    if (ix.empty())
        THROW(("No index specified"));

    LOG(INFO, "handling browse request for db=%s, ix=%s",
              db.c_str(), ix.c_str());

    M6Databank* mdb = Load(db);
    if (mdb == nullptr)
        THROW(("Databank not loaded"));

    string iFirst = params.get("first", "").as<string>();
    string iLast = params.get("last", "").as<string>();

    sub.put("db", db);
    sub.put("ix", ix);
    sub.put("first", iFirst);
    sub.put("last", iLast);

    vector<pair<string,string>> sections;
    if (mdb->BrowseSectionsForIndex(ix, iFirst, iLast, 30, sections))
    {
        vector<el::object> elSections;

        for (auto& section : sections)
        {
            el::object elSection;
            elSection["first"] = section.first;
            elSection["last"] = section.second;
            elSections.push_back(elSection);
        }

        sub.put("sections", elSections.begin(), elSections.end());
    }
    else
    {
        vector<string> keys;
        mdb->ListIndexEntries(ix, iFirst, iLast, keys);

        for (auto& key : keys)
            LOG (DEBUG, "key for %s - %s: %s", iFirst.c_str (), iLast.c_str (), key.c_str ());

        sub.put("keys", keys.begin(), keys.end());
    }

    create_reply_from_template("browse.html", sub, reply);

    LOG(INFO, "done generating browse response for db=%s, ix=%s",
              db.c_str(), ix.c_str());
}

// --------------------------------------------------------------------

string M6Server::get_hashed_password(const string& username, const string& realm)
{
    string result;

    boost::format f("/mrs-config/users/user[@name='%1%' and @realm='%2%']");
    if (zx::element* user = mConfig->find_first((f % username % realm).str()))
    {
        result = user->get_attribute("password");
    }

    return result;
}

// --------------------------------------------------------------------

void RunMainLoop(uint32 inNrOfThreads, bool redirectOutputToLog)
{
    for (;;)
    {
        if (redirectOutputToLog)
        {
            LOG(DEBUG,"RunMainLoop: redirecting output to log");

            // (re-)open the log files.
            fs::path logfile = fs::path(M6Config::GetDirectory("log")) / "access.log";
            fs::path errfile = fs::path(M6Config::GetDirectory("log")) / "error.log";
            OpenLogFile(logfile.string(), errfile.string());
        }

        LOG(DEBUG,"RunMainLoop: importing namespaces");

        using namespace boost::local_time;
        using namespace boost::posix_time;

        local_time_facet* lf(new local_time_facet("[%d/%b/%Y:%H:%M:%S %z]"));
        cerr.imbue(std::locale(std::cout.getloc(), lf));
        cerr << local_date_time(second_clock::local_time(), time_zone_ptr())
             << " Restarting services...";

        LOG(DEBUG,"RunMainLoop: blocking signals");

        M6SignalCatcher catcher;
        catcher.BlockSignals();

        LOG(DEBUG,"RunMainLoop: get config");

        const zx::element* config = M6Config::GetServer();
        if (config == nullptr)
            THROW(("Missing server configuration"));

        string addr = config->get_attribute("addr");
        string port = config->get_attribute("port");

        if (port.empty())
            port = "80";

        LOG(DEBUG,"RunMainLoop: configuring server");

        shared_ptr<M6Server> server(new M6Server(config));

        LOG(DEBUG,"RunMainLoop: binding server to %s:%s",addr.c_str(),port.c_str());

        server->bind(addr, boost::lexical_cast<uint16>(port));
        boost::thread thread(boost::bind(&zeep::http::server::run, boost::ref(*server), inNrOfThreads));

        cerr << " done" << endl
             << local_date_time(second_clock::local_time(), time_zone_ptr())
             << " listening at " << addr << ':' << port << endl;

        LOG(DEBUG,"RunMainLoop: unblocking signals");

        catcher.UnblockSignals();

        LOG(DEBUG,"RunMainLoop: waiting for signals..");

        int sig;
        do
        {
            sig = catcher.WaitForSignal();

            // signal logging
            string sigstr;
            switch(sig)
            {
            case SIGINT : sigstr = "SIGINT";  break;
            case SIGHUP : sigstr = "SIGHUP";  break;
            case SIGSEGV: sigstr = "SIGSEGV"; break;
            case SIGQUIT: sigstr = "SIGQUIT"; break;
            case SIGTERM: sigstr = "SIGTERM"; break;
            default: sigstr = to_string(sig); break;
            }

            LOG(WARN,"RunMainLoop: recieved signal: %s",sigstr.c_str());

            //cerr << local_date_time(second_clock::local_time(), time_zone_ptr()) << " RunMainLoop recieved signal: " << sigstr << endl;
        }
        while (sig == SIGCHLD); // we don't care about these in MRS server

        server->stop();

        LOG(DEBUG,"RunMainLoop: stopped server");

#ifdef BOOST_CHRONO_EXTENSIONS
        if (not thread.try_join_for(boost::chrono::seconds(5)))
#else
        if (not thread.timed_join(boost::posix_time::seconds(5)))
#endif
        {
            thread.interrupt();
            thread.detach();
        }

        LOG(DEBUG,"RunMainLoop: ending iteration");

        if (sig == SIGHUP)
            continue;

        break;
    }
}

int M6Server::Start(const string& inRunAs, const string& inPidFile, bool inForeground)
{
#if ! defined(_MSC_VER)
    // enable the dumping of cores to enable postmortem debugging
    rlimit l;
    if (getrlimit(RLIMIT_CORE, &l) == 0)
    {
        l.rlim_cur = l.rlim_max;
        if (l.rlim_cur == 0 or setrlimit(RLIMIT_CORE, &l) < 0)
            cerr << "Failed to set rlimit" << endl;
    }
#endif

    int result = 0;

    const zx::element* config = M6Config::GetServer();

    string runas = inRunAs;
    if (runas.empty())
        runas= config->get_attribute("user");

    string pidfile = inPidFile;
    if (pidfile.empty())
        pidfile = config->get_attribute("pidfile");

    // check to see if we're running already
    if (not inForeground and IsPIDFileForExecutable(pidfile))
    {
        cout << "Server is already running.\n";

        result = 1;
    }
    else
    {
        // make sure we can listen to the port before forking off as daemon

        try
        {
            string addr = config->get_attribute("addr");
            string port = config->get_attribute("port");
            if (port.empty())
                port = "80";

            boost::asio::io_service io_service;
            boost::asio::ip::tcp::resolver resolver(io_service);
            boost::asio::ip::tcp::resolver::query query(addr, port);
            boost::asio::ip::tcp::endpoint endpoint(*resolver.resolve(query));

            boost::asio::ip::tcp::acceptor acceptor(io_service);
            acceptor.open(endpoint.protocol());
            acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor.bind(endpoint);
            acceptor.listen();

            LOG(DEBUG,"M6Server::Start: listening at %s:%s",addr.c_str(),port.c_str());
        }
        catch (exception& e)
        {
            THROW(("Is mrs running already? %s", e.what()));
        }

        if (not inForeground)
        {
            LOG(DEBUG,"M6Server::Start: deamonizing");
            Daemonize(runas, pidfile);
        }

        (void)M6Scheduler::Instance();

        uint32 nrOfThreads = boost::thread::hardware_concurrency();
        //if (vm.count("threads"))
        //    nrOfThreads = vm["threads"].as<uint32>();
        RunMainLoop(nrOfThreads, not inForeground);

        if (not pidfile.empty() and fs::exists(pidfile))
        {
            try { fs::remove(pidfile); } catch (...) {}
        }
    }

    LOG(DEBUG,"M6Server::Start: returning result %d",result);

    return result;
}

int M6Server::Stop(const string& inPidFile)
{
    int result = 1;

    const zx::element* config = M6Config::GetServer();

    string pidfile = inPidFile;
    if (pidfile.empty())
        pidfile = config->get_attribute("pidfile");

    if (IsPIDFileForExecutable(pidfile))
    {
        ifstream file(pidfile);
        if (not file.is_open())
            THROW(("Failed to open pid file"));

        int pid;
        file >> pid;

        result = StopDaemon(pid);

        file.close();
        try
        {
            if (fs::exists(pidfile))
                fs::remove(pidfile);
        }
        catch (...) {}

    } else {
        THROW(("Not my pid file: %s", pidfile.c_str()));
    }

    return result;
}

int M6Server::Status(const string& inPidFile)
{
    const zx::element* config = M6Config::GetServer();

    string pidfile = inPidFile;
    if (pidfile.empty())
        pidfile = config->get_attribute("pidfile");

    int result;

    if (IsPIDFileForExecutable(pidfile))
    {
        if (VERBOSE)
            cerr << "mrs server is running" << endl;
        result = 0;
    }
    else
    {
        if (VERBOSE)
            cerr << "mrs server is not running" << endl;
        result = 1;
    }

    return result;
}

int M6Server::Reload(const string& inPidFile)
{
    const zx::element* config = M6Config::GetServer();

    string pidfile = inPidFile;
    if (pidfile.empty())
        pidfile = config->get_attribute("pidfile");

    int result;

    if (IsPIDFileForExecutable(pidfile))
    {
        ifstream file(pidfile);
        if (not file.is_open())
            THROW(("Failed to open pid file"));

        int pid;
        file >> pid;

        result = KillDaemon(pid, SIGHUP);
    }
    else
    {
        if (VERBOSE)
            cerr << "mrs server is not running" << endl;
        result = 1;
    }

    return result;
}

