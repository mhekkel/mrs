#include "M6Lib.h"

#include <iostream>

#include <boost/test/unit_test.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

#include "M6Builder.h"
#include "M6Databank.h"
#include "M6Document.h"

using namespace std;
namespace ba = boost::algorithm;

BOOST_AUTO_TEST_CASE(test_build_1)
{
    cout << "test building databanks - 1" << endl;

    unique_ptr<M6Databank> databank(M6Databank::CreateNew("test/test-db.m6"));

    M6Lexicon lexicon;
    databank->StartBatchImport(lexicon);

    boost::format idFmt("ID_%05.5d");

    for (uint32 i = 1; i <= 1000; ++i)
    {
        string id = (idFmt % i).str();

        M6InputDocument* doc = new M6InputDocument(*databank, id);
        doc->SetAttribute("id", id);

        ba::to_lower(id);
        doc->IndexText("id", eM6ValueIndex, id, false);

        doc->Tokenize(lexicon, 0);
        databank->Store(doc);
    }

    databank->CommitBatchImport();
}

BOOST_AUTO_TEST_CASE(test_build_2)
{
    cout << "test building databanks - 2" << endl;

    M6Databank databank("test/test-db.m6", eReadOnly);
    BOOST_CHECK_EQUAL(databank.size(), 1000);

    boost::format idFmt("ID_%05.5d");

    for (uint32 i = 1; i <= databank.size(); ++i)
    {
        string id = (idFmt % i).str();

        M6Document* doc = databank.Fetch(i);

        BOOST_CHECK(doc != nullptr);

        if (doc == nullptr)
            continue;

        BOOST_CHECK_EQUAL(doc->GetAttribute("id"), id);
        BOOST_CHECK_EQUAL(doc->GetText(), id);

        delete doc;
    }
}

BOOST_AUTO_TEST_CASE(test_build_3)
{
    cout << "test building databanks - 3" << endl;

    M6Databank databank("test/test-db.m6", eReadOnly);
    BOOST_CHECK_EQUAL(databank.size(), 1000);

    boost::format idFmt("ID_%05.5d");

    for (uint32 i = 1; i <= databank.size(); ++i)
    {
        string id = (idFmt % i).str();

        M6Document* doc = databank.FindDocument("id", id);

        BOOST_CHECK(doc != nullptr);

        if (doc == nullptr)
            continue;

        BOOST_CHECK_EQUAL(doc->GetAttribute("id"), id);
        BOOST_CHECK_EQUAL(doc->GetText(), id);

        delete doc;
    }
}

BOOST_AUTO_TEST_CASE(test_build_4)
{
    cout << "testing builder (pdbfinder 1)" << endl;

    M6Builder builder("pdbfinder");
    builder.Build();
}

