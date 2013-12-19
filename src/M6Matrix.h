//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#pragma once

extern const int8 kMBlosum45[], kMBlosum50[], kMBlosum62[], kMBlosum80[], kMBlosum90[],
	kMPam250[], kMPam30[], kMPam70[];
extern const float kMPam250ScalingFactor, kMPam250MisMatchAverage;

struct MatrixStats
{
	double	lambda, kappa, entropy, alpha, beta;
};

struct MatrixData
{
	const char*	mName;
	int8		mGapOpen, mGapExtend;
	const int8*	mMatrix;
	MatrixStats	mGappedStats, mUngappedStats;
};

extern const MatrixData kMatrixData[];

// Simple scoring function using the predefined matrices
template<typename T>
inline T score(const T inMatrix[], uint8 inAA1, uint8 inAA2)
{
	T result;

	if (inAA1 >= inAA2)
		result = inMatrix[(inAA1 * (inAA1 + 1)) / 2 + inAA2];
	else
		result = inMatrix[(inAA2 * (inAA2 + 1)) / 2 + inAA1];

	return result;	
}

