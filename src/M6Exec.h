#pragma once

#include <vector>
#include <string>

#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/read.hpp>

int ForkExec(std::vector<const char*>& args, double maxRunTime,
	const std::string& in, std::string& out, std::string& err);

class M6Process
{
  public:
	
	typedef char char_type;
	struct category : boost::iostreams::input_filter_tag, boost::iostreams::multichar_tag {};

	template<typename Source>
	std::streamsize read(Source& src, char* s, std::streamsize n)
	{
		// pump as much data as possible
		while (mOutputBufferPtr != nullptr)
		{
			if (mOutputBufferPtr == mOutputBufferEnd)
			{
				mOutputBufferPtr = mOutputBufferEnd = mOutputBuffer;
				std::streamsize result = boost::iostreams::read(src, mOutputBuffer, sizeof(mOutputBuffer));
				if (result == -1)
					mOutputBufferPtr = mOutputBufferEnd = nullptr;
				else
					mOutputBufferEnd += result;
			}
			
			if (mOutputBufferPtr != mOutputBufferEnd)
			{
				std::streamsize n = mOutputBufferEnd - mOutputBufferPtr;
				if (WriteOutputBuffer() != n)
					break;
			}
		}
		
		return ReadInputBuffer(s, n);
	}
	
							M6Process(const std::vector<const char*>& args);
							M6Process(const M6Process&);
	M6Process&				operator=(const M6Process&);
							~M6Process();

  private:

	std::streamsize			WriteOutputBuffer();
	std::streamsize			ReadInputBuffer(char* s, std::streamsize n);

	struct M6ProcessImpl*	mImpl;
	char					mInputBuffer[4096];
	char*					mInputBufferPtr;
	char*					mInputBufferEnd;
	char					mOutputBuffer[4096];
	char*					mOutputBufferPtr;
	char*					mOutputBufferEnd;
};

