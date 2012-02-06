#pragma once

class M6IBitStream;
class M6OBitStream;

// --------------------------------------------------------------------

class M6OBitStream
{
  public:
						M6OBitStream();
	explicit			M6OBitStream(M6File& inFile);
						M6OBitStream(const M6OBitStream& inStream);
						~M6OBitStream();
	M6OBitStream&		operator=(const M6OBitStream& inStream);
	void				swap(M6OBitStream& ioStream);

	//

	M6OBitStream&		operator<<(bool inBit);
	void				Sync();
	size_t				Size() const;
	bool				Empty() const		{ return BitSize() == 0; }
	void				Clear();
	size_t				BitSize() const		{ return Size() * 8 + (7 - mBitOffset); }

	size_t				Copy(void* outBuffer, size_t inBufferSize) const;
	void				Write(M6File& inFile) const;
	void				GetBits(int64& outBits) const;

  private:

	void				Add(uint8 inByte);
	void				Overflow();
	
	enum {
		kBufferSize = 22
	};
	
	struct M6OBitStreamImpl*
						mImpl;
	uint8				mData[kBufferSize];
	uint8				mByteOffset;
	int8				mBitOffset;
};

namespace std {
	template<> inline void swap(M6OBitStream& a, M6OBitStream& b)
		{ a.swap(b); }
}

// --------------------------------------------------------------------

class M6IBitStream
{
  public:
						M6IBitStream();
						M6IBitStream(M6File& inFile, int64 inOffset);
						M6IBitStream(const void* inData, size_t inSize);
	explicit			M6IBitStream(const M6OBitStream& inBits);
	M6IBitStream&		operator=(const M6IBitStream& inStream);
						~M6IBitStream();
	
	// 
	
	int					operator()() const;
	void				Sync();
	bool				Eof() const;
	void				Underflow();
	size_t				ByteRead() const;
	void				Skip(uint32 inBits);
	
  private:

	struct M6IBitStreamImpl
	{
						M6IBitStreamImpl(size_t inSize);
		
		void			Get(uint8& outByte);
		
		size_t			mSize;
		size_t			mByteOffset;
		
	  protected:
		void			Read();

		uint8*			mBufferPtr;
		size_t			mBufferSize;
	};


	void				NextByte(uint8& outByte);
	
	
	int32				mBitOffset;
	uint8				mByte;
};

// --------------------------------------------------------------------
//	Basic routines for reading/writing numbers in bit streams
//	
//	Binary mode writes a fixed bitcount number of bits for a value
//
template<class T>
void ReadBinary(M6IBitStream& inBits, int32 inBitCount, T& outValue)
{
	assert(inBitCount <= sizeof(T) * 8);
	assert(inBitCount > 0);
	outValue = 0;

	uint64 b = 1ULL << (inBitCount - 1);
	while (b)
	{
		if (inBits())
			outValue |= b;
		b >>= 1;
	}
}

template<class T> void WriteBinary(M6OBitStream& inBits, int inBitCount, const T& inValue)
{
	assert(inBitCount <= int32(sizeof(T) * 8));
	assert(inBitCount > 0);
	
	uint64 b = 1ULL << (inBitCount - 1);
	while (b)
	{
		inBits << (inValue & b);
		b >>= 1;
	}
}

//	
//	Gamma mode writes a variable number of bits for a value, optimal for small numbers
//
template<class T> void ReadGamma(M6IBitStream& inBits, T& outValue)
{
	outValue = 1;
	int32 e = 0;
	
	while (inBits() and outValue != 0)
	{
		outValue <<= 1;
		++e;
	}

	assert(outValue != 0);
	
	T v2 = 0;
	while (e-- > 0)
		v2 = (v2 << 1) | inBits();
	
	outValue += v2;
}

template<class T> void WriteGamma(M6OBitStream& inBits, const T& inValue)
{
	assert(inValue > 0);
	
	T v = inValue;
	int32 e = 0;
	
	while (v > 1)
	{
		v >>= 1;
		++e;
		inBits << 1;
	}
	inBits << 0;
	
	v = inValue - (1LL << e);
	
	uint64 b = 1ULL << (e - 1);
	while (b)
	{
		inBits << (v & b);
		b >>= 1;
	}
}

void ReadBits(M6IBitStream& inBits, M6OBitStream& outValue);
void WriteBits(M6OBitStream& inBits, const M6OBitStream& inValue);
void CopyBits(M6OBitStream& inBits, const M6OBitStream& inValue);

// --------------------------------------------------------------------
//	Arrays are a bit more complex

template<class T> void WriteArray(M6OBitStream& inBits, T& inArray, int64 inMax);
template<class T> void ReadArray(M6IBitStream& inBits, T& outArray, int64 inMax);
