//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <zeep/dispatcher.hpp>
#include <boost/shared_ptr.hpp>
#include <list>

#include <zeep/xml/node.hpp>

class M6Server;

namespace M6WSBlastNS
{

struct Parameters
{
	boost::optional<std::string>	matrix;
	boost::optional<uint32>			wordSize;
	boost::optional<double>			expect;
	boost::optional<bool>			lowComplexityFilter;
	boost::optional<bool>			gapped;
	boost::optional<uint32>			gapOpen;
	boost::optional<uint32>			gapExtend;
	
									Parameters();
									Parameters(boost::optional<Parameters>& rhs);
	
	template<class Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & BOOST_SERIALIZATION_NVP(matrix)
		   & BOOST_SERIALIZATION_NVP(wordSize)
		   & BOOST_SERIALIZATION_NVP(expect)
		   & BOOST_SERIALIZATION_NVP(lowComplexityFilter)
		   & BOOST_SERIALIZATION_NVP(gapped)
		   & BOOST_SERIALIZATION_NVP(gapOpen)
		   & BOOST_SERIALIZATION_NVP(gapExtend);
	}
};

enum JobStatus
{
	unknown,
	queued,
	running,
	error,
	finished
};

struct Hsp
{
	uint32				score;
	double				bitScore;
	double				expect;
	uint32				queryStart;
	uint32				subjectStart;
	uint32				identity;
	uint32				positive;
	uint32				gaps;
	uint32				subjectLength;
	std::string			queryAlignment;
	std::string			subjectAlignment;
	std::string			midline;

	template<class Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & BOOST_SERIALIZATION_NVP(score)
		   & BOOST_SERIALIZATION_NVP(bitScore)
		   & BOOST_SERIALIZATION_NVP(expect)
		   & BOOST_SERIALIZATION_NVP(queryStart)
		   & BOOST_SERIALIZATION_NVP(subjectStart)
		   & BOOST_SERIALIZATION_NVP(identity)
		   & BOOST_SERIALIZATION_NVP(positive)
		   & BOOST_SERIALIZATION_NVP(gaps)
		   & BOOST_SERIALIZATION_NVP(subjectLength)
		   & BOOST_SERIALIZATION_NVP(queryAlignment)
		   & BOOST_SERIALIZATION_NVP(subjectAlignment)
		   & BOOST_SERIALIZATION_NVP(midline);
	}
};

struct Hit
{
	std::string						id;
	boost::optional<std::string>	sequenceId;
	std::string						title;
	std::vector<Hsp>				hsps;

	template<class Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & BOOST_SERIALIZATION_NVP(id)
		   & BOOST_SERIALIZATION_NVP(sequenceId)
		   & BOOST_SERIALIZATION_NVP(title)
		   & BOOST_SERIALIZATION_NVP(hsps);
	}
};

struct BlastResult
{
	uint32				dbCount;
	uint64				dbLength;
	uint64				effectiveSearchSpace;
	double				kappa;
	double				lambda;
	double				entropy;
	std::vector<Hit>	hits;

	template<class Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & BOOST_SERIALIZATION_NVP(dbCount)
		   & BOOST_SERIALIZATION_NVP(dbLength)
		   & BOOST_SERIALIZATION_NVP(effectiveSearchSpace)
		   & BOOST_SERIALIZATION_NVP(kappa)
		   & BOOST_SERIALIZATION_NVP(lambda)
		   & BOOST_SERIALIZATION_NVP(entropy)
		   & BOOST_SERIALIZATION_NVP(hits);
	}
};

}

class M6WSBlast : public zeep::dispatcher
{
  public:
				M6WSBlast(M6Server& inServer, const std::string& inNS, const std::string& inService);
	virtual		~M6WSBlast();
	
	void		Blast(const std::string& query, const std::string& program, const std::string& db,
					boost::optional<M6WSBlastNS::Parameters> params,
					boost::optional<uint32> reportLimit, std::string& jobId);
		
	void		BlastJobStatus(std::string jobId, M6WSBlastNS::JobStatus& status);
	void		BlastJobResult(std::string jobId, M6WSBlastNS::BlastResult& result);
	void		BlastJobError(std::string jobId, std::string& error);
	
  protected:
	M6Server&	mServer;
};
