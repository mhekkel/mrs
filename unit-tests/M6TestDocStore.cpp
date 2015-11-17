#include <iostream>
#include <ios>
#include <fstream>
#include <map>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <zeep/xml/document.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
//#include <boost/timer/timer.hpp>
#include <boost/regex.hpp>

#include "M6Lib.h"
#include "M6File.h"
#include "M6Index.h"
#include "M6Tokenizer.h"
#include "M6Error.h"
#include "M6Lexicon.h"
#include "M6DocStore.h"
#include "M6Document.h"
#include "M6Databank.h"

#include <boost/test/unit_test.hpp>

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

vector<string> testdocs;

BOOST_AUTO_TEST_CASE(test_store_0)
{
    cout << "testing document store (initialising)" << endl;

    ifstream text("test/pdbfind2-head.txt");
    BOOST_REQUIRE(text.is_open());

    stringstream doc;

    for (;;)
    {
        string line;
        getline(text, line);

        if (line.empty())
        {
            if (text.eof())
                break;
            continue;
        }

        doc << line << endl;

        if (line == "//")
        {
            testdocs.push_back(doc.str());
            doc.str("");
            doc.clear();
        }
    }
}

BOOST_AUTO_TEST_CASE(test_store_1)
{
    cout << "testing document store (store)" << endl;

    if (fs::exists("test/pdbfind2.docs"))
        fs::remove("test/pdbfind2.docs");

//    boost::timer::auto_cpu_timer t;

    M6DocStore store("test/pdbfind2.docs", eReadWrite);

    for (const string& doc : testdocs)
        store.StoreDocument(doc.c_str(), doc.length());
    store.Commit();

//    store.Dump();
    store.Validate();

    BOOST_CHECK_EQUAL(store.size(), testdocs.size());
}

BOOST_AUTO_TEST_CASE(test_store_2)
{
    cout << "testing document store (retrieve-1)" << endl;

//    boost::timer::auto_cpu_timer t;

    M6DocStore store("test/pdbfind2.docs", eReadOnly);

    store.Validate();

    for (uint32 i = 1; i <= testdocs.size(); ++i)
    {
        uint32 docPage, docSize;
        BOOST_CHECK(store.FetchDocument(i, docPage, docSize));

        io::filtering_stream<io::input> is;
        store.OpenDataStream(i, docPage, docSize, is);

        string docA;
        for (;;)
        {
            string line;
            getline(is, line);
            if (line.empty() and is.eof())
                break;
            docA += line + "\n";
        }

        string docB = testdocs[i - 1];

        BOOST_CHECK_EQUAL(docA.length(), docB.length());
        BOOST_CHECK_EQUAL(docA, docB);
    }
}

BOOST_AUTO_TEST_CASE(test_store_3)
{
    cout << "testing document store (retrieve-2 one line only)" << endl;

//    boost::timer::auto_cpu_timer t;

    M6DocStore store("test/pdbfind2.docs", eReadOnly);

    store.Validate();

    for (uint32 i = 1; i <= testdocs.size(); ++i)
    {
        uint32 docPage, docSize;
        BOOST_CHECK(store.FetchDocument(i, docPage, docSize));

        io::filtering_stream<io::input> is;
        store.OpenDataStream(i, docPage, docSize, is);

        string line;
        getline(is, line);
    }
}

BOOST_AUTO_TEST_CASE(test_store_4)
{
    cout << "testing document store (store using M6Databank)" << endl;

    boost::regex re("^ID\\s*:\\s+(.{4})");

//    boost::timer::auto_cpu_timer t;

    if (fs::exists("test/pdbfind2.m6"))
        fs::remove_all("test/pdbfind2.m6");

    M6Databank db("test/pdbfind2.m6", eReadWrite);

    M6Lexicon lexicon;
    db.StartBatchImport(lexicon);

    for (const string& text : testdocs)
    {
        M6InputDocument* doc = new M6InputDocument(db, text);

        boost::smatch m;
        BOOST_REQUIRE(boost::regex_search(text, m, re));

        string attr(m[1]);
        doc->SetAttribute("id", attr.c_str(), attr.length());

        db.Store(doc);
    }

    db.CommitBatchImport();

    db.Validate();

    BOOST_CHECK_EQUAL(db.size(), testdocs.size());
}

BOOST_AUTO_TEST_CASE(test_store_5)
{
    cout << "testing document store (retrieve using M6Databank)" << endl;

//    boost::timer::auto_cpu_timer t;

    M6Databank db("test/pdbfind2.m6", eReadOnly);

    db.Validate();

    BOOST_CHECK_EQUAL(db.size(), testdocs.size());

    for (uint32 i = 1; i <= testdocs.size(); ++i)
    {
        M6Document* doc = db.Fetch(i);

        BOOST_CHECK(doc != nullptr);

        if (doc == nullptr)
            continue;

        BOOST_CHECK_EQUAL(doc->GetText(), testdocs[i - 1]);

        delete doc;
    }
}

BOOST_AUTO_TEST_CASE(test_store_6)
{
    cout << "testing document store (retrieve attribute)" << endl;

    boost::regex re("^ID\\s*:\\s+(.{4})");

//    boost::timer::auto_cpu_timer t;

    M6Databank db("test/pdbfind2.m6", eReadOnly);
    BOOST_CHECK_EQUAL(db.size(), testdocs.size());

    for (uint32 i = 1; i <= testdocs.size(); ++i)
    {
        M6Document* doc = db.Fetch(i);

        BOOST_CHECK(doc != nullptr);

        if (doc == nullptr)
            continue;

        boost::smatch m;
        BOOST_REQUIRE(boost::regex_search(testdocs[i - 1], m, re));
        BOOST_CHECK_EQUAL(doc->GetAttribute("id"), m[1]);

        delete doc;
    }
}

BOOST_AUTO_TEST_CASE(test_store_7)
{
    cout << "dumping pdbfinder" << endl;

//    boost::timer::auto_cpu_timer t;

    M6Databank db("test/pdbfinder.m6", eReadOnly);
    uint32 size = db.size();

    for (uint32 i = 1; i <= size; ++i)
    {
        M6Document* doc = db.Fetch(i);

        BOOST_CHECK(doc != nullptr);

        if (doc == nullptr)
            continue;

        (void)doc->GetAttribute("id");
        (void)doc->GetAttribute("title");

        delete doc;
    }
}
