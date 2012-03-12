#include "M6Lib.h"

#include <cassert>
#include <limits>
#include <cstring>

#include "M6BitStream.h"
#include "M6File.h"
#include "M6Error.h"

using namespace std;

// --------------------------------------------------------------------
//	M6OBitStream

struct M6OBitStreamFileImpl : public M6OBitStreamImpl
{
	static const size_t kFileBufferSize = 16384;

					M6OBitStreamFileImpl(M6File& inFile)
						: mFile(inFile), mBufferPtr(mBuffer) {}
					~M6OBitStreamFileImpl();

	virtual size_t	Size() const								{ return mFile.Size() + (mBufferPtr - mBuffer); }
	virtual void	Write(const uint8* inData, size_t inSize);
	virtual void	Truncate()									{ mFile.Truncate(0); mBufferPtr = mBuffer; }
					
  private:
	M6File&			mFile;
	char			mBuffer[kFileBufferSize];
	char*			mBufferPtr;
};

M6OBitStreamFileImpl::~M6OBitStreamFileImpl()
{
	if (mBufferPtr > mBuffer)
		mFile.Write(mBuffer, mBufferPtr - mBuffer);
}

void M6OBitStreamFileImpl::Write(const uint8* inData, size_t inSize)
{
	size_t free = kFileBufferSize - (mBufferPtr - mBuffer);
	if (free >= inSize)
	{
		memcpy(mBufferPtr, inData, inSize);
		mBufferPtr += inSize;
	}
	else
	{
		size_t n = free;
		if (n > inSize)
			n = inSize;
		
		if (n > 0)
		{
			memcpy(mBufferPtr, inData, n);
			mBufferPtr += n;
			inData += n;
			inSize -= n;
		}
		
		assert(mBufferPtr == mBuffer + kFileBufferSize);
		mFile.Write(mBuffer, kFileBufferSize);
		mBufferPtr = mBuffer;
		
		while (inSize > kFileBufferSize)
		{
			mFile.Write(inData, kFileBufferSize);
			inSize -= kFileBufferSize;
			inData += kFileBufferSize;
		}
		
		memcpy(mBufferPtr, inData, inSize);
		mBufferPtr += inSize;
	}
}

struct M6OBitStreamMemImpl : public M6OBitStreamImpl
{
					M6OBitStreamMemImpl();
	virtual			~M6OBitStreamMemImpl();

	virtual size_t	Size() const								{ return mBufferPtr - mBuffer; }
	virtual void	Write(const uint8* inData, size_t inSize);
	virtual void	Truncate()									{ mBufferPtr = mBuffer; }

	uint8*			mBuffer;
	uint8*			mBufferPtr;
	size_t			mBufferSize;
};

M6OBitStreamMemImpl::M6OBitStreamMemImpl()
{
	mBufferSize = kM6DefaultBitBufferSize;
	mBufferPtr = mBuffer = new uint8[mBufferSize];
}

M6OBitStreamMemImpl::~M6OBitStreamMemImpl()
{
	delete[] mBuffer;
}

void M6OBitStreamMemImpl::Write(const uint8* inData, size_t inSize)
{
	size_t size = Size();
	if (size + inSize > mBufferSize)
	{
		uint8* t = new uint8[2 * mBufferSize];
		memcpy(t, mBuffer, size);
		delete[] mBuffer;
		mBufferSize *= 2;
		mBuffer = t;
		mBufferPtr = mBuffer + size;
	}
	
	memcpy(mBufferPtr, inData, inSize);
	mBufferPtr += inSize;
}

// --------------------------------------------------------------------

M6OBitStream::M6OBitStream()
	: mImpl(nullptr)
	, mByteOffset(0)
	, mBitOffset(7)
{
	mData[mByteOffset] = 0;
}

M6OBitStream::M6OBitStream(M6File& inFile)
	: mImpl(new M6OBitStreamFileImpl(inFile))
	, mByteOffset(0)
	, mBitOffset(7)
{
	mData[mByteOffset] = 0;
}

M6OBitStream::~M6OBitStream()
{
	if (mImpl != nullptr)
		mImpl->Release();
}

M6OBitStream::M6OBitStream(const M6OBitStream& inStream)
	: mImpl(inStream.mImpl)
	, mByteOffset(inStream.mByteOffset)
	, mBitOffset(inStream.mBitOffset)
{
	memcpy(mData, inStream.mData, mByteOffset + 1);

	if (mImpl != nullptr)
		mImpl->Reference();
}

M6OBitStream& M6OBitStream::operator=(const M6OBitStream& inStream)
{
	if (this != &inStream)
	{
		if (mImpl != nullptr)
			mImpl->Release();
	
		mImpl = inStream.mImpl;
	
		if (mImpl != nullptr)
			mImpl->Reference();
	
		mByteOffset = inStream.mByteOffset;
		mBitOffset = inStream.mBitOffset;
		assert(mByteOffset < kBufferSize);
		memcpy(mData, inStream.mData, mByteOffset + 1);
	}

	return *this;
}

void M6OBitStream::swap(M6OBitStream& __x)
{
	std::swap(mImpl, __x.mImpl);
	std::swap(mBitOffset, __x.mBitOffset);
	std::swap(mByteOffset, __x.mByteOffset);
	
	int32 k = mByteOffset;
	if (k < __x.mByteOffset)
		k = __x.mByteOffset;
	
	assert(k < kBufferSize);
	
	for (int32 i = 0; i <= k; ++i)
		std::swap(mData[i], __x.mData[i]);
}

void M6OBitStream::Overflow()
{
	if (mImpl == nullptr)
		mImpl = new M6OBitStreamMemImpl;

	if (mByteOffset > 0)
	{
		mImpl->Write(mData, mByteOffset);
		mByteOffset = 0;
	}
}

void M6OBitStream::Sync()
{
	(*this) << 0;
	
	while (mBitOffset != 7)
		(*this) << 1;
	
	if (mImpl != nullptr)
		Overflow();
}

void M6OBitStream::Clear()
{
	if (mImpl != nullptr)
		mImpl->Truncate();
	
	mByteOffset = 0;
	mBitOffset = 7;
	mData[mByteOffset] = 0;
}

const uint8* M6OBitStream::Peek() const
{
	const uint8* result = mData;
	
	if (mImpl != nullptr)
	{
		M6OBitStreamMemImpl* impl = dynamic_cast<M6OBitStreamMemImpl*>(mImpl);
		assert(impl);
		if (impl == nullptr)
			THROW(("Peek called for invalid obitstream"));
		
		// make sure sync is called
		assert(mBitOffset == 7);
		assert(mByteOffset == 0);
		result = impl->mBuffer;
	}
	
	return result;
}

inline void M6OBitStream::Add(uint8 byte)
{
	mData[mByteOffset] |= (byte >> (7 - mBitOffset));
	if (++mByteOffset >= kBufferSize)
		Overflow();
	mData[mByteOffset] = (byte << (1 + mBitOffset));
}

size_t M6OBitStream::Size() const
{
	size_t result = mByteOffset;
	if (mBitOffset < 7)
		result += 1;

	if (mImpl != nullptr)
		result += mImpl->Size();

	return result;
}

// --------------------------------------------------------------------
//	M6IBitStream

struct M6IBitStreamFileImpl : public M6IBitStreamImpl
{
					M6IBitStreamFileImpl(M6File& inData, int64 inOffset, uint32 inBitBufferSize);
					M6IBitStreamFileImpl(const M6IBitStreamFileImpl& inImpl);

	virtual void	Read();

	virtual M6IBitStreamImpl*
					Clone()					{ return new (mBitBufferSize) M6IBitStreamFileImpl(*this); }

	void*			operator new(size_t inSize, uint32 inBitBufferSize);
	void			operator delete(void* inPtr, uint32);

	M6File&			mData;
	int64			mOffset;
	uint32			mBitBufferSize;
	char			mDataBuffer[1];
};

M6IBitStreamFileImpl::M6IBitStreamFileImpl(M6File& inData, int64 inOffset, uint32 inBitBufferSize)
	: mData(inData)
	, mOffset(inOffset)
	, mBitBufferSize(inBitBufferSize)
{
}

M6IBitStreamFileImpl::M6IBitStreamFileImpl(const M6IBitStreamFileImpl& inImpl)
	: M6IBitStreamImpl(inImpl), mData(inImpl.mData), mOffset(inImpl.mOffset), mBitBufferSize(inImpl.mBitBufferSize)
{
	memcpy(mDataBuffer, inImpl.mBufferPtr, mBufferSize);
	mBufferPtr = reinterpret_cast<uint8*>(mDataBuffer);
}

void M6IBitStreamFileImpl::Read()
{
	mBufferSize = mBitBufferSize;

	if (mBufferSize > mData.Size() - mOffset)
		mBufferSize = mData.Size() - mOffset;

	mData.PRead(mDataBuffer, mBufferSize, mOffset);
	mOffset += mBufferSize;
	mBufferPtr = reinterpret_cast<uint8*>(mDataBuffer);
}

void* M6IBitStreamFileImpl::operator new(size_t inSize, uint32 inBitBufferSize)
{
	return ::malloc(inSize + inBitBufferSize);
}

void M6IBitStreamFileImpl::operator delete(void* inPtr, uint32)
{
	::free(inPtr);
}

// --------------------------------------------------------------------

struct M6IBitStreamOBitImpl : public M6IBitStreamImpl
{
					M6IBitStreamOBitImpl(const M6OBitStream& inData)
						: mData(&inData)
					{
						if (mData->mImpl != nullptr)
						{
							M6OBitStreamMemImpl* impl = dynamic_cast<M6OBitStreamMemImpl*>(mData->mImpl);
							assert(impl);
							mBufferPtr = impl->mBuffer;
							mBufferSize = impl->Size();
						}
						else
						{
							mBufferPtr = const_cast<uint8*>(mData->mData);
							mBufferSize = mData->Size();
						}
					}

					M6IBitStreamOBitImpl(const M6IBitStreamOBitImpl& inImpl)
						: M6IBitStreamImpl(inImpl), mData(inImpl.mData) {}

	virtual void	Read()
					{
						if (mData->mImpl != nullptr)
						{
							mBufferPtr = const_cast<uint8*>(mData->mData);
							mBufferSize = mData->Size();
						}
					}

	virtual M6IBitStreamImpl*
					Clone()					{ return new M6IBitStreamOBitImpl(*this); }

	const M6OBitStream*	mData;
};

// --------------------------------------------------------------------

M6IBitStream::M6IBitStream()
	: mImpl(nullptr)
	, mBitOffset(7)
{
}

M6IBitStream::M6IBitStream(M6IBitStreamImpl* inImpl)
	: mImpl(inImpl)
	, mBitOffset(7)
{
	mImpl->Get(mByte);
}

M6IBitStream::M6IBitStream(M6File& inData, int64 inOffset, uint32 inBitBufferSize)
	: mImpl(new (inBitBufferSize) M6IBitStreamFileImpl(inData, inOffset, inBitBufferSize))
	, mBitOffset(7)
{
	mImpl->Get(mByte);
}

M6IBitStream::M6IBitStream(const M6OBitStream& inData)
	: mImpl(new M6IBitStreamOBitImpl(inData))
	, mBitOffset(7)
{
	mImpl->Get(mByte);
}

M6IBitStream::M6IBitStream(const M6IBitStream& inStream)
	: mImpl(inStream.mImpl)
	, mBitOffset(inStream.mBitOffset)
	, mByte(inStream.mByte)
{
	if (mImpl != nullptr)
		mImpl = mImpl->Clone();
}

M6IBitStream& M6IBitStream::operator=(const M6IBitStream& inStream)
{
	if (this != &inStream)
	{
		delete mImpl;
		mImpl = inStream.mImpl;
		if (mImpl != nullptr)
			mImpl = mImpl->Clone();
		mBitOffset = inStream.mBitOffset;
		mByte = inStream.mByte;
	}
	
	return *this;
}

M6IBitStream::~M6IBitStream()
{
	delete mImpl;
}

//void M6IBitStream::Sync()
//{
//	// replay the sync from an obit stream's sync
//	
//	int bit = operator()();
//	assert(bit == 0);
//	
//	while (mBitOffset != 7)
//		bit = operator()();
//}
//
//// skip forward as fast as possible
//void M6IBitStream::Skip(uint32 inBits)
//{
//	if (inBits == 0)
//		return;
//	
//	if (inBits >= static_cast<uint32>(mBitOffset + 1))
//	{
//		inBits -= mBitOffset + 1;
//		mImpl->Get(mByte);
//		mBitOffset = 7;
//	}
//	
//	while (inBits >= 8)
//	{
//		inBits -= 8;
//		mImpl->Get(mByte);
//	}
//	
//	mBitOffset -= inBits;
//}

void M6IBitStream::NextByte(uint8& outByte)
{
	outByte = mByte << (7 - mBitOffset);
	mImpl->Get(mByte);
	outByte |= mByte >> (mBitOffset + 1);
}

// --------------------------------------------------------------------
//	Functions

void ReadBits(M6IBitStream& inBits, M6OBitStream& outValue)
{
	int64 length;
	ReadGamma(inBits, length);

	outValue.Clear();
	
	while (length >= 8)
	{
		uint8 byte;
		inBits.NextByte(byte);
		outValue.Add(byte);
		length -= 8;
	}
	
	while (length-- > 0)
		outValue << inBits();
}

void WriteBits(M6OBitStream& inBits, const M6OBitStream& inValue)
{
	int64 length = inValue.BitSize();
	WriteGamma(inBits, length);
	CopyBits(inBits, inValue);
}

void CopyBits(M6OBitStream& inBits, const M6OBitStream&	inValue)
{
	M6IBitStream bits(inValue);
	int64 bitCount = inValue.BitSize();

	while (bitCount >= 8)
	{
		uint8 byte;
		bits.NextByte(byte);
		inBits.Add(byte);
		bitCount -= 8;
	}
	
	while (bitCount-- > 0)
		inBits << bits();
}

// --------------------------------------------------------------------
//	Arrays
//	This is a simplified version of the array compression routines in MRS
//	Only supported datatype is now uint32 and only supported width it 32 bit.

struct M6Selector
{
	int32		databits;
	uint32		span;
};

const M6Selector kSelectors[16] = {
	{  0, 1 },
	{ -3, 1 },
	{ -2, 1 }, { -2, 2 },
	{ -1, 1 }, { -1, 2 }, { -1, 4 },
	{  0, 1 }, {  0, 2 }, {  0, 4 },
	{  1, 1 }, {  1, 2 }, {  1, 4 },
	{  2, 1 }, {  2, 2 },
	{  3, 1 }
};

const uint32
	kMaxWidth = 32, kStartWidth = kMaxWidth / 2;

inline uint32 Select(int32 inBitsNeeded[], uint32 inCount,
	int32 inWidth, const M6Selector inSelectors[])
{
	uint32 result = 0;
	int32 c = inBitsNeeded[0] - kMaxWidth;
	
	for (uint32 i = 1; i < 16; ++i)
	{
		if (inSelectors[i].span > inCount)
			continue;
		
		int32 w = inWidth + inSelectors[i].databits;
		
		if (w > kMaxWidth or w < 0)
			continue;
		
		bool fits = true;
		int32 waste = 0;
		
		switch (inSelectors[i].span)
		{
			case 4:
				fits = fits and inBitsNeeded[3] <= w;
				waste += w - inBitsNeeded[3];
			case 3:
				fits = fits and inBitsNeeded[2] <= w;
				waste += w - inBitsNeeded[2];
			case 2:
				fits = fits and inBitsNeeded[1] <= w;
				waste += w - inBitsNeeded[1];
			case 1:
				fits = fits and inBitsNeeded[0] <= w;
				waste += w - inBitsNeeded[0];
		}
		
		if (fits == false)
			continue;
		
		int32 n = (inSelectors[i].span - 1) * 4 - waste;
		
		if (n > c)
		{
			result = i;
			c = n;
		}
	}
	
	return result;
}

inline void Shift(std::vector<uint32>::const_iterator& ioIterator,
	uint32& ioLast, uint32& outDelta, int32& outWidth)
{
	uint32 next = *ioIterator++;
	assert(next > ioLast);
	
	outDelta = next - ioLast - 1;
	ioLast = next;
	
	uint32 v = outDelta;
	outWidth = 0;
	while (v > 0)
	{
		v >>= 1;
		++outWidth;
	}
}

void CompressSimpleArraySelector(M6OBitStream& inBits, const vector<uint32>& inArray)
{
	int32 width = kStartWidth;
	uint32 last = 0;
	
	int32 bn[4];
	uint32 dv[4];
	uint32 bc = 0;
	vector<uint32>::const_iterator a = inArray.begin();
	vector<uint32>::const_iterator e = inArray.end();
	
	while (a != e or bc > 0)
	{
		while (bc < 4 and a != e)
		{
			Shift(a, last, dv[bc], bn[bc]);
			++bc;
		}
		
		uint32 s = Select(bn, bc, width, kSelectors);

		if (s == 0)
			width = kMaxWidth;
		else
			width += kSelectors[s].databits;

		uint32 n = kSelectors[s].span;
		
		WriteBinary(inBits, 4, s);

		if (width > 0)
		{
			for (uint32 i = 0; i < n; ++i)
				WriteBinary(inBits, width, dv[i]);
		}
		
		bc -= n;

		if (bc > 0)
		{
			for (uint32 i = 0; i < (4 - n); ++i)
			{
				bn[i] = bn[i + n];
				dv[i] = dv[i + n];
			}
		}
	}
}

// --------------------------------------------------------------------
//	M6CompressedArray

M6CompressedArray::M6CompressedArray()
{
}

M6CompressedArray::M6CompressedArray(const M6CompressedArray& inArray)
	: mBits(inArray.mBits)
	, mSize(inArray.mSize)
{
}

M6CompressedArray::M6CompressedArray(const M6IBitStream& inBits, uint32 inLength)
	: mBits(inBits)
	, mSize(inLength)
{
}

M6CompressedArray& M6CompressedArray::operator=(const M6CompressedArray& inArray)
{
	if (this != &inArray)
	{
		mBits = inArray.mBits;
		mSize = inArray.mSize;
	}
	return *this;
}

M6CompressedArray::const_iterator::const_iterator(const M6IBitStream& inBits, uint32 inLength)
	: mBits(inBits), mCount(inLength), mWidth(kStartWidth), mSpan(0), mCurrent(0)
{
	operator++();
}

M6CompressedArray::const_iterator& M6CompressedArray::const_iterator::operator++()
{
	if (mCount != sSentinel and mCount > 0)
	{
		if (mSpan == 0)
		{
			uint32 selector;
			ReadBinary(mBits, 4, selector);
			mSpan = kSelectors[selector].span;
			
			if (selector == 0)
				mWidth = kMaxWidth;
			else
				mWidth += kSelectors[selector].databits;
		}

		if (mWidth > 0)
		{
			uint32 delta;
			ReadBinary(mBits, mWidth, delta);
			mCurrent += delta;
		}

		mCurrent += 1;

		--mSpan;
		--mCount;
	}
	else
		mCount = sSentinel;

	return *this;
}

// --------------------------------------------------------------------
//	Array Routines

void WriteArray(M6OBitStream& inBits, const vector<uint32>& inArray)
{
	uint32 cnt = static_cast<uint32>(inArray.size());
	WriteGamma(inBits, cnt);
	assert(cnt == 0 or inArray.front() > 0);
	if (not inArray.empty() and inArray.front() == 0)
		THROW(("Invalid array, should not contain zero"));
	CompressSimpleArraySelector(inBits, inArray);
}

void ReadArray(M6IBitStream& inBits, vector<uint32>& outArray)
{
	outArray.clear();
	
	uint32 size;
	ReadGamma(inBits, size);
	outArray.reserve(size);

	uint32 width = kStartWidth;
	uint32 span = 0;
	uint32 current = 0;

	while (size-- > 0)
	{
		if (span == 0)
		{
			uint32 selector;
			ReadBinary(inBits, 4, selector);
			span = kSelectors[selector].span;
			
			if (selector == 0)
				width = kMaxWidth;
			else
				width += kSelectors[selector].databits;
		}

		if (width > 0)
		{
			uint32 delta;
			ReadBinary(inBits, width, delta);
			current += delta;
		}

		current += 1;

		outArray.push_back(current);

		--span;
	}
}
