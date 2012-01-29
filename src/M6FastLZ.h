/*
	FastLZ is based on the fastlz library by Ariya Hidayat.
	See http://www.fastlz.org/ for more information.
*/

#pragma once

// maxout should be length + length / 20 or length + 5 whichever is greater

size_t FastLZCompress(const void* input, size_t length,
	void* output, size_t maxout);

size_t FastLZDecompress(const void* input, size_t length,
	void* output, uint32 maxout);
