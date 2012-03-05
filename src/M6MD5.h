#pragma once

#include <string>

class M6MD5
{
  public:
				M6MD5();
				M6MD5(const void* inData, size_t inLength);
				M6MD5(const std::string& inData);

	void		Update(const void* inData, size_t inLength);
	void		Update(const std::string& inData)
					{ Update(inData.c_str(), inData.length()); }

	std::string	Finalise();

  private:

	void		Transform(const uint8* inData);

	uint32		mBuffer[4];
	uint8		mData[64];
	uint32		mDataLength;
	int64		mBitLength;
};
