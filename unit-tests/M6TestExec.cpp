#include <string>
#include <vector>
#include <iostream>
#include <sstream>

#include <boost/thread.hpp>
#define BOOST_TEST_MODULE BlastTest
#include <boost/test/included/unit_test.hpp>

#include "M6Exec.h"

int VERBOSE = 1;

BOOST_AUTO_TEST_CASE(TestClustal)
{
    std::vector<const char*> args;
    args.push_back("/usr/bin/clustalo");
    args.push_back("-i");
    args.push_back("-");
    args.push_back(nullptr);

    std::string fasta = ">1\nTTCCPSIVARSNFNVCRLPGTPICATYTGCIIIPGATCPGDYAN\n>2\nTTCCPSIVARSNFNVCRLPGTPEALCATYTGCIIIPGATCPGDYAN";

    std::stringstream in(fasta), out, err;

    ForkExec(args, 30, in, out, err);

    BOOST_CHECK_EQUAL("", err.str());

    std::cout << out.str() << std::endl;
}
