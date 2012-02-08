#pragma once

#include <vector>

class M6IBitStream;
class M6OBitStream;
class M6File;
class M6FileStream;

// --------------------------------------------------------------------

class M6OBitStream
{
  public:
						M6OBitStream();
	explicit			M6OBitStream(M6FileStream& inFile);
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

//	size_t				Copy(void* outBuffer, size_t inBufferSize) const;
//	void				Write(M6FileStream& inFile) const;
//	void				GetBits(int64& outBits) const;

	friend class M6IBitStream;
	friend struct M6IBitStreamOBitImpl;
	friend void ReadBits(M6IBitStream& inBits, M6OBitStream& outValue);
	friend void WriteBits(M6OBitStream& inBits, const M6OBitStream& inValue);
	friend void CopyBits(M6OBitStream& inBits, const M6OBitStream& inValue);

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
//						M6IBitStream(const void* inData, size_t inSize);
						M6IBitStream(const M6IBitStream& inBits);
	explicit			M6IBitStream(const M6OBitStream& inBits);
	M6IBitStream&		operator=(const M6IBitStream& inStream);
						~M6IBitStream();
	
	// 
	
	int					operator()();
	void				Sync();
	bool				Eof() const					{ return (7 - mBitOffset) >= (8 + mImpl->mSize); }
	void				Underflow();
	size_t				BytesRead() const			{ return mImpl->mByteOffset; }
	void				Skip(uint32 inBits);
	
	friend void ReadBits(M6IBitStream& inBits, M6OBitStream& outValue);
	friend void WriteBits(M6OBitStream& inBits, const M6OBitStream& inValue);
	friend void CopyBits(M6OBitStream& inBits, const M6OBitStream& inValue);

  private:

	friend struct M6IBitStreamFileImpl;
	friend struct M6IBitStreamOBitImpl;

	struct M6IBitStreamImpl
	{
						M6IBitStreamImpl(size_t inSize);
		virtual			~M6IBitStreamImpl() {}
		
		void			Reference();
		void			Release();

		void			Get(uint8& outByte);
		
		int64			mSize;
		int64			mByteOffset;
		
	  protected:
		virtual void	Read() = 0;

		uint8*			mBufferPtr;
		int64			mBufferSize;
		int32			mRefCount;
	};

	void				NextByte(uint8& outByte);
	
	M6IBitStreamImpl*	mImpl;
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
	
	uint64 b = 1ULL << e;
	while (e-- > 0)
	{
		b >>= 1;
		inBits << (inValue & b);
	}
}

void ReadBits(M6IBitStream& inBits, M6OBitStream& outValue);
void WriteBits(M6OBitStream& inBits, const M6OBitStream& inValue);
void CopyBits(M6OBitStream& inBits, const M6OBitStream& inValue);

// --------------------------------------------------------------------
//	Arrays are a bit more complex

//template<class T> void WriteArray(M6OBitStream& inBits, T& inArray, int64 inMax);
//template<class T> void ReadArray(M6IBitStream& inBits, T& outArray, int64 inMax);

//class M6CompressedArrayIterator
//{
//  public:
//				M6CompressedArrayIterator(M6OBitStream& inBits);
//				~M6CompressedArrayIterator();
//	bool		Next(uint32& outValue);
//};

void WriteArray(M6OBitStream& inBits, std::vector<uint32>& inArray, int64 inMax);
void ReadArray(M6OBitStream& inBits, std::vector<uint32>& outArray, int64 inMax);

//class M6CompressedWeightArrayIterator
//{
//  public:
//				M6CompressedArrayIterator(M6OBitStream& inBits);
//				~M6CompressedArrayIterator();
//	bool		Next(uint32& outValue, uint8& outWeight);
//};
//
//void WriteArray(M6OBitStream& inBits, std::vector<std::pair<uint32,uint8>>& inArray, int64 inMax);
//void ReadArray(M6OBitStream& inBits, std::vector<std::pair<uint32,uint8>>& outArray, int64 inMax);


// --------------------------------------------------------------------
//
//	Inline implementations
//

// --------------------------------------------------------------------

inline M6OBitStream& M6OBitStream::operator<<(bool inBit)
{
	if (inBit)
		mData[mByteOffset] |= 1 << mBitOffset;
	
	if (--mBitOffset < 0)
	{
		++mByteOffset;
		mBitOffset = 7;
		
		if (mByteOffset >= kBufferSize)
			Overflow();
		
		mData[mByteOffset] = 0;
	}
	
	return *this;
}

// --------------------------------------------------------------------

inline void M6IBitStream::M6IBitStreamImpl::Get(uint8& outByte)
{
	if (mBufferSize <= 0)
		Read();
	
	if (mBufferSize > 0)
	{
		outByte = *mBufferPtr++;
		mSize -= 8;
		++mByteOffset;
		--mBufferSize;
	}
	else
		outByte = 0;
}

inline void M6IBitStream::Underflow()
{
	if (not Eof())
	{
		mImpl->Get(mByte);
		mBitOffset = 7;
	}
}

inline int M6IBitStream::operator()()
{
	int result = (mByte & (1 << mBitOffset)) != 0;
	--mBitOffset;
	
	if (mBitOffset < 0)
		Underflow();
	
	return result;
}

// --------------------------------------------------------------------

