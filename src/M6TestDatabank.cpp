#include "M6Lib.h"

#include <iostream>

#include <boost/test/unit_test.hpp>
#include <boost/timer/timer.hpp>

#include "M6Databank.h"

using namespace std;

//BOOST_AUTO_TEST_CASE(test_databank_0)
//{
//	cout << "testing pdbfinder" << endl;
//
//	M6Databank databank("./test/pdbfinder.m6", eReadOnly);
//	databank.Validate();
//}

BOOST_AUTO_TEST_CASE(test_databank_1)
{
	cout << "testing pdbfinder" << endl;

	boost::timer::auto_cpu_timer t;

	M6Databank databank("./test/pdbfinder.m6", eReadWrite);
	databank.RecalculateDocumentWeights();
}
