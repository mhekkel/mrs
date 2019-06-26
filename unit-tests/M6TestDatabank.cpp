#include <string>
#include <vector>
#include <cstdint>

#define BOOST_TEST_MODULE DatabankTest
#include <boost/test/included/unit_test.hpp>

#include "M6Config.h"
#include "M6Query.h"
#include "M6Iterator.h"
#include "M6Databank.h"
#include "M6Builder.h"
#include "M6Server.h"


int VERBOSE = 0;

BOOST_AUTO_TEST_CASE(TestDatabank1)
{
    std::string dbName = "test-pdbfinder";

    M6Config::SetConfigFilePath("unit-tests/data/mrs-config.xml");

    M6Builder builder(dbName);
    builder.Build(1);

    const zx::element* config = M6Config::GetServer();
    M6Server server(config);

    std::vector<el::object> hits;
    uint32 hitCount;
    bool ranked;
    std::string parseError;

    server.Find(dbName, "1xxx", true, 0, 10, true,
                hits, hitCount, ranked, parseError);
    BOOST_CHECK(hitCount > 0);

    server.Find(dbName, "exp_method:x", true, 0, 10, true,
                hits, hitCount, ranked, parseError);
    BOOST_CHECK(hitCount > 0);

    server.Find(dbName, "Resolution < 1.2", true, 0, 10, true,
                hits, hitCount, ranked, parseError);
    BOOST_CHECK(hitCount > 0);

    server.Find(dbName, "Resolution > 1.2", true, 0, 10, true,
                hits, hitCount, ranked, parseError);
    BOOST_CHECK(hitCount <= 0);

    server.Find(dbName, "hssp_n_align > 100", true, 0, 10, true,
                hits, hitCount, ranked, parseError);
    BOOST_CHECK(hitCount > 0);
}
