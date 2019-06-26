#include <boost/thread.hpp>
#define BOOST_TEST_MODULE BlastTest
#include <boost/test/included/unit_test.hpp>

#include "M6Config.h"
#include "M6Lib.h"
#include "M6Blast.h"


BOOST_AUTO_TEST_CASE(TestBlast1)
{
    M6Config::SetConfigFilePath("unit-tests/data/mrs-config.xml");

    std::string matrix("BLOSUM62"), program = "blastp",
                query = ">gnl|pdb|1CRN|A\nTTCCPSIVARSNFNVCRLPGTPEAICATYTGCIIIPGATCPGDYAN";
    int32 gapOpen = -1, gapExtend = -1, wordSize = 0,
          threads = boost::thread::hardware_concurrency(), reportLimit = 250;
    bool filter = true, gapped = true;
    double expect = 10;
    std::vector<boost::filesystem::path> databanks;
    databanks.push_back("unit-tests/data/cram_craab.fasta");

    M6Blast::Result* r = M6Blast::Search(databanks, query, program, matrix,
                                         wordSize, expect, filter, gapped,
                                         gapOpen, gapExtend, reportLimit,
                                         threads);
    BOOST_CHECK(r != NULL);
    BOOST_CHECK(r->mHits.size() > 0);

    M6Blast::Hit *hit = &(r->mHits.front());
    BOOST_CHECK(hit->mHsps.size() > 0);

    M6Blast::Hsp *hsp = &(hit->mHsps.front());
    BOOST_CHECK_EQUAL(hsp->mIdentity, 45);
    BOOST_CHECK_EQUAL(hsp->mMidLine.compare(
                        "TTCCPSIVARSNFNVCRLPGTPEA+CATYTGCIIIPGATCPGDYAN"),
                      0);
}
