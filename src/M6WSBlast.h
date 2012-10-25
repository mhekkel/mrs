#pragma once

#include <zeep/server.hpp>
#include <boost/shared_ptr.hpp>
#include <list>

#include <zeep/xml/node.hpp>

namespace M6WSBlastNS
{

struct Parameters
{
	std::string					matrix;
	uint32						wordSize;
	double						expect;
	bool						lowComplexityFilter;
	bool						gapped;
	uint32						gapOpen;
	uint32						gapExtend;
	
								Parameters();
	
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
	std::string			id;
	std::vector<std::string>		// make this a vector so it won't be an empty
						sequenceId;	// string in case we have no chain
	std::string			title;
	std::vector<Hsp>	hsps;

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

class M6WSBlast : public zeep::server
{
  public:
						M6WSBlast(const zeep::xml::element* inConfig);
	virtual				~M6WSBlast();
	
	void				Blast(const std::string& query,
							const std::string& program, const std::string& db,
							const std::string& mrsBooleanQuery,
							const M6WSBlastNS::Parameters& params,
							uint32 reportLimit, std::string& jobId);
		
	void				BlastJobStatus(std::string jobId,
							M6WSBlastNS::JobStatus& status);
						
	void				BlastJobResult(std::string jobId,
							M6WSBlastNS::BlastResult& result);
					
	void				BlastJobError(std::string jobId, std::string& error);
	
  protected:
	std::vector<std::string>
						mDbTable;
};