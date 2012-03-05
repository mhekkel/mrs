#pragma once

#include <iterator>
#include <vector>
#include <cassert>

class M6IBitStream;
class M6OBitStream;
class M6File;

// --------------------------------------------------------------------

struct M6OBitStreamImpl
{
					M6OBitStreamImpl() : mRefCount(1)	{ }
	virtual 		~M6OBitStreamImpl()					{ assert(mRefCount == 0); }
	void			Reference()							{ ++mRefCount; }
	void			Release()							{ if (--mRefCount == 0) delete this; }

	virtual size_t	Size() const = 0;
	virtual void	Write(const uint8* inData, size_t inSize) = 0;
	virtual void	Truncate() = 0;

  private:
					M6OBitStreamImpl(const M6OBitStreamImpl&);
	M6OBitStreamImpl& operator=(const M6OBitStreamImpl&);

	uint32			mRefCount;
};

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
	const uint8*		Peek() const;
	void				Sync();
	size_t				Size() const;
	bool				Empty() const		{ return BitSize() == 0; }
	void				Clear();
	size_t				BitSize() const		{ return Size() * 8 + (7 - mBitOffset); }

//	size_t				Copy(void* outBuffer, size_t inBufferSize) const;
//	void				Write(M6File& inFile) const;
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
	
	M6OBitStreamImpl*	mImpl;
	uint8				mData[kBufferSize];
	uint8				mByteOffset;
	int8				mBitOffset;
};

namespace std {
	template<> inline void swap(M6OBitStream& a, M6OBitStream& b)
		{ a.swap(b); }
}

// --------------------------------------------------------------------

struct M6IBitStreamImpl
{
					M6IBitStreamImpl()
						: mBufferPtr(nullptr), mBufferSize(0)
					{
					}
					
					M6IBitStreamImpl(const M6IBitStreamImpl& inImpl)
						: mBufferPtr(inImpl.mBufferPtr), mBufferSize(inImpl.mBufferSize)
					{
					}

	// using Clone, we can make ibitstreams copy constructable.
	virtual M6IBitStreamImpl* Clone() = 0;

	void			Get(uint8& outByte);
	
  protected:
	virtual void	Read() = 0;

	uint8*			mBufferPtr;
	int64			mBufferSize;
};

class M6IBitStream
{
  public:
						M6IBitStream();
						M6IBitStream(M6IBitStreamImpl* inImpl);
						M6IBitStream(M6File& inFile, int64 inOffset);
						M6IBitStream(const M6IBitStream& inBits);
	explicit			M6IBitStream(const M6OBitStream& inBits);
	M6IBitStream&		operator=(const M6IBitStream& inStream);
						~M6IBitStream();
	
	// 
	
	int					operator()();
//	void				Sync();
	void				Underflow();
//	void				Skip(uint32 inBits);
	
	friend void ReadBits(M6IBitStream& inBits, M6OBitStream& outValue);
	friend void WriteBits(M6OBitStream& inBits, const M6OBitStream& inValue);
	friend void CopyBits(M6OBitStream& inBits, const M6OBitStream& inValue);

	friend struct M6IBitStreamFileImpl;
	friend struct M6IBitStreamOBitImpl;

  private:

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

inline void WriteBinary(M6OBitStream& inBits, int inBitCount, uint64 inValue)
{
	assert(inBitCount <= 64);
	assert(inBitCount > 0);
	
	while (inBitCount-- > 0)
		inBits << (inValue & (1ULL << inBitCount));
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

// ReadArray/WriteArray are simple routines to write out an array
// of unsigned integers that are ordered, first element should be > 0
// The size of the array is stored inside the bitstream.

void ReadArray(M6IBitStream& inBits, std::vector<uint32>& outArray);
void WriteArray(M6OBitStream& inBits, const std::vector<uint32>& inArray);

// Lower level access to arrays, the CompressSimpleArraySelector
// writes out the same array as WriteArray, but without the size.
// So you need to store that value elsewhere.

void CompressSimpleArraySelector(M6OBitStream& inBits, const std::vector<uint32>& inArray);

// To iterate over array elements stored in a bitstream, you can use
// the M6CompressedArray class. It has a const_iterator for your convenience.

class M6CompressedArray
{
  public:
				M6CompressedArray();
				M6CompressedArray(const M6CompressedArray& inArray);
				M6CompressedArray(const M6IBitStream& inBits, uint32 inLength);
	M6CompressedArray&
				operator=(const M6CompressedArray& inArray);
	
	struct const_iterator : public std::iterator<std::forward_iterator_tag, const uint32>
	{
		typedef std::iterator<std::forward_iterator_tag, const uint32>	base_type;
		typedef base_type::reference									reference;
		typedef base_type::pointer										pointer;
		
						const_iterator();
						const_iterator(const const_iterator& iter);
						const_iterator(const M6IBitStream& inBits, uint32 inCount);
		const_iterator&	operator=(const const_iterator& iter);

		reference		operator*() const								{ return mCurrent; }
		pointer			operator->() const								{ return &mCurrent; }

		const_iterator&	operator++();
		const_iterator	operator++(int)									{ const_iterator iter(*this); operator++(); return iter; }

		bool			operator==(const const_iterator& iter) const	{ return mCount == iter.mCount; }
		bool			operator!=(const const_iterator& iter) const	{ return not operator==(iter); }

	  private:
		static const uint32 sSentinel = ~0;

		M6IBitStream	mBits;
		uint32			mCount;
		int32			mWidth;
		uint32			mSpan;
		uint32			mCurrent;
	};
	
	const_iterator		begin() const;
	const_iterator		end() const;
	
	uint32				size() const									{ return mSize; }
	bool				empty() const									{ return mSize == 0; }

  private:
	M6IBitStream		mBits;
	uint32				mSize;
};

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

inline void M6IBitStreamImpl::Get(uint8& outByte)
{
	if (mBufferSize <= 0)
		Read();
	
	if (mBufferSize > 0)
	{
		outByte = *mBufferPtr++;
		--mBufferSize;
	}
	else
		outByte = 0;
}

inline void M6IBitStream::Underflow()
{
	mImpl->Get(mByte);
	mBitOffset = 7;
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

inline M6CompressedArray::const_iterator::const_iterator()
	: mCount(sSentinel), mWidth(0), mSpan(0), mCurrent(0)
{
}

inline M6CompressedArray::const_iterator::const_iterator(const const_iterator& iter)
	: mBits(iter.mBits)
	, mCount(iter.mCount)
	, mWidth(iter.mWidth)
	, mSpan(iter.mSpan)
	, mCurrent(iter.mCurrent)
{
}

inline M6CompressedArray::const_iterator& M6CompressedArray::const_iterator::operator=(const const_iterator& iter)
{
	if (this != &iter)
	{
		mBits = iter.mBits;
		mCount = iter.mCount;
		mWidth = iter.mWidth;
		mSpan = iter.mSpan;
		mCurrent = iter.mCurrent;
	}
	
	return *this;
}

inline M6CompressedArray::const_iterator M6CompressedArray::begin() const
{
	return const_iterator(mBits, mSize);
}

inline M6CompressedArray::const_iterator M6CompressedArray::end() const
{
	return const_iterator();
}
