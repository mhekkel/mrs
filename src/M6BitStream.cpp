#include "M6Lib.h"

#include <cassert>
#include <limits>
#include <cstring>

#include "M6BitStream.h"
#include "M6File.h"
#include "M6Error.h"

using namespace std;

// --------------------------------------------------------------------
//	constants

const uint32
	kBitBufferSize = 64,
	kBitBufferExtend = 512;

const int64
	kUnboundedBufferSize = numeric_limits<int64>::max();

// --------------------------------------------------------------------
//	M6OBitStream

struct M6OBitStreamFileImpl : public M6OBitStreamImpl
{
					M6OBitStreamFileImpl(M6FileStream& inFile)
						: mFile(inFile) {}

	virtual size_t	Size() const								{ return mFile.Size(); }
	virtual void	Write(const void* inData, size_t inSize)	{ mFile.Write(inData, inSize); }
	virtual void	Truncate()									{ mFile.Truncate(0); }
					
	M6FileStream&	mFile;
};

struct M6OBitStreamMemImpl : public M6OBitStreamImpl
{
					M6OBitStreamMemImpl();
	virtual			~M6OBitStreamMemImpl();

	virtual size_t	Size() const								{ return mBufferPtr - mBuffer; }
	virtual void	Write(const void* inData, size_t inSize);
	virtual void	Truncate()									{ mBufferPtr = mBuffer; }

	uint8*			mBuffer;
	uint8*			mBufferPtr;
	size_t			mBufferSize;
};

M6OBitStreamMemImpl::M6OBitStreamMemImpl()
{
	mBufferSize = 256;
	mBufferPtr = mBuffer = new uint8[mBufferSize];
}

M6OBitStreamMemImpl::~M6OBitStreamMemImpl()
{
	delete[] mBuffer;
}

void M6OBitStreamMemImpl::Write(const void* inData, size_t inSize)
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

M6OBitStream::M6OBitStream(M6FileStream& inFile)
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

//int64 M6OBitStream::Copy(void* outData, int64 inSize)
//{
//	int64 result = 0;
//	
//	assert(mBitOffset == 7);
//	if (mImpl != nullptr)
//		result = mImpl->Read(outData, inSize);
//	else
//	{
//		assert(inSize <= mByteOffset);
//		memcpy(outData, mData, inSize);
//	}
//	
//	return result;
//}

inline void M6OBitStream::Add(uint8 byte)
{
	mData[mByteOffset] |= (byte >> (7 - mBitOffset));
	if (++mByteOffset >= kBufferSize)
		Overflow();
	mData[mByteOffset] = (byte << (1 + mBitOffset));
}

//void M6OBitStream::Write(M6FileStream& inFile) const
//{
//	assert(mBitOffset == 7);	// assert we're synced!
//	
//	if (mImpl != nullptr)
//		;
////		mImpl->CopyTo(inFile, mImpl->file->Size(), 0);
//	else
//		inFile.Write(mData, mByteOffset);
//}

//void M6OBitStream::GetBits(int64& outBits) const
//{
//	assert(BitSize() <= 63);
//
//	uint64 value = 0x8000000000000000ULL |
//		((int64(mData[0]) << 55) & 0x7F80000000000000ULL) |
//		((int64(mData[1]) << 47) & 0x007F800000000000ULL) |
//		((int64(mData[2]) << 39) & 0x00007F8000000000ULL) |
//		((int64(mData[3]) << 31) & 0x0000007F80000000ULL) |
//		((int64(mData[4]) << 23) & 0x000000007F800000ULL) |
//		((int64(mData[5]) << 15) & 0x00000000007F8000ULL) |
//		((int64(mData[6]) <<  7) & 0x0000000000007F80ULL) |
//		((int64(mData[7]) >>  1) & 0x000000000000007FULL);
//
//	uint64 mask = ~0ULL;
//	mask <<= (63 - BitSize());
//	
//	outBits = (int64)(value & mask);
//}

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

// --------------------------------------------------------------------
//
//struct M6IBitStreamConstImpl : public M6IBitStreamImpl
//{
//					M6IBitStreamConstImp(const uint8* inData, int64 inSize = -1);
//	virtual void	Read()			{ assert(false); }	// should never be called
//};
//
//M6IBitStreamConstImp::M6IBitStreamConstImp(const uint8* inData, int64 inSize)
//	: M6IBitStreamImpl(inSize)
//{
//	mBufferPtr = const_cast<uint8*>(inData);
//
//	if (mSize > 0)	// exact size is known
//	{
//		mBufferSize = inSize;
//		
//		uint8 byte = mBufferPtr[mBufferSize - 1];
//		
//		int32 mBitOffset = 0;
//	
//		while (mBitOffset < 7 and byte & (1 << mBitOffset) and mSize > 0)
//			++mBitOffset, --mSize;
//	
//		assert(mSize > 0);
//		--mSize;
//	}
//	else
//		mSize = mBufferSize = kUnboundedBufferSize;
//}

// --------------------------------------------------------------------

struct M6IBitStreamFileImpl : public M6IBitStreamImpl
{
					M6IBitStreamFileImpl(M6File& inData, int64 inOffset, int64 inSize = -1);
					M6IBitStreamFileImpl(const M6IBitStreamFileImpl& inImpl);

	virtual void	Read();
	virtual M6IBitStreamImpl*
					Clone()					{ return new M6IBitStreamFileImpl(*this); }

	M6File&			mData;
	int64			mOffset;
	char			mDataBuffer[kBitBufferExtend];
};

M6IBitStreamFileImpl::M6IBitStreamFileImpl(M6File& inData, int64 inOffset, int64 inSize)
	: M6IBitStreamImpl(inSize)
	, mData(inData)
	, mOffset(inOffset)
{
	if (inSize > 0)
	{
		uint8 byte;
		
		mData.PRead(&byte, 1, mOffset + inSize - 1);
		
		int32 mBitOffset = 0;
		while (mBitOffset < 7 and byte & (1 << mBitOffset) and mSize > 0)
			++mBitOffset, --mSize;

		assert(mSize > 0);
		--mSize;
	}
	else
		mSize = kUnboundedBufferSize;
}

M6IBitStreamFileImpl::M6IBitStreamFileImpl(const M6IBitStreamFileImpl& inImpl)
	: M6IBitStreamImpl(inImpl), mData(inImpl.mData), mOffset(inImpl.mOffset)
{
	memcpy(mDataBuffer, inImpl.mDataBuffer, mBufferSize);
}

void M6IBitStreamFileImpl::Read()
{
	//if (mData.Tell() == 0)				// avoid reading excess bytes in the first call
		mBufferSize = kBitBufferSize;
	//else
	//	mBufferSize = kBitBufferExtend;

	if (mBufferSize > mSize / 8 + 1)
		mBufferSize = mSize / 8 + 1;
	
	if (mBufferSize > mData.Size() - mOffset)
		mBufferSize = mData.Size() - mOffset;

	mData.PRead(mDataBuffer, mBufferSize, mOffset);
	mOffset += mBufferSize;
	mBufferPtr = reinterpret_cast<uint8*>(mDataBuffer);
}

// --------------------------------------------------------------------

struct M6IBitStreamOBitImpl : public M6IBitStreamImpl
{
					M6IBitStreamOBitImpl(const M6OBitStream& inData)
						: M6IBitStreamImpl(inData.BitSize())
						, mData(&inData)
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

M6IBitStream::M6IBitStream(M6File& inData, int64 inOffset)
	: mImpl(new M6IBitStreamFileImpl(inData, inOffset))
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
	: mImpl(inStream.mImpl->Clone())
	, mBitOffset(inStream.mBitOffset)
	, mByte(inStream.mByte)
{
}

M6IBitStream& M6IBitStream::operator=(const M6IBitStream& inStream)
{
	if (this != &inStream)
	{
		delete mImpl;
		mImpl = inStream.mImpl->Clone();
		mBitOffset = inStream.mBitOffset;
		mByte = inStream.mByte;
	}
	
	return *this;
}

M6IBitStream::~M6IBitStream()
{
	delete mImpl;
}

void M6IBitStream::Sync()
{
	// replay the sync from an obit stream's sync
	
	int bit = operator()();
	assert(bit == 0);
	
	while (mBitOffset != 7)
		bit = operator()();
}

// skip forward as fast as possible
void M6IBitStream::Skip(uint32 inBits)
{
	if (inBits == 0)
		return;
	
	if (inBits >= static_cast<uint32>(mBitOffset + 1))
	{
		inBits -= mBitOffset + 1;
		mImpl->Get(mByte);
		mBitOffset = 7;
	}
	
	while (inBits >= 8)
	{
		inBits -= 8;
		mImpl->Get(mByte);
	}
	
	mBitOffset -= inBits;
}

void M6IBitStream::NextByte(uint8& outByte)
{
	assert(not Eof() or mBitOffset == 7);
	
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

const uint32 kMaxWidth = 32;

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
	int32 width = kMaxWidth;
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

//template<typename T, uint32 K>
//class IteratorBase
//{
//  public:
//					IteratorBase(CIBitStream& inData, int64 inMax)
//						: fBits(&inData)
//						, fRead(0)
//						, fValue(-1)
//						, fMax(inMax)
//					{
//						int64 v = fMax;
//						fMaxWidth = 0;
//						while (v > 0)
//						{
//							v >>= 1;
//							++fMaxWidth;
//						}
//
//						Reset();
//					}
//
//	virtual			~IteratorBase() {}
//
//	virtual bool	Next()
//					{
//						bool done = false;
//					
//						if (fRead < fCount)
//						{
//							if (fSpan == 0)
//							{
//								uint32 selector;
//								ReadBinary(*fBits, 4, selector);
//								fSpan = kSelectors[selector].span;
//								
//								if (selector == 0)
//									fWidth = fMaxWidth;
//								else
//									fWidth += kSelectors[selector].databits;
//							}
//					
//							if (fWidth > 0)
//							{
//								int64 delta;
//								ReadBinary(*fBits, fWidth, delta);
//								fValue += delta;
//							}
//
//							fValue += 1;
//					
//							--fSpan;
//							++fRead;
//							
//							done = true;
//						}
//					
//						return done;
//					}
//	
//	T				Value() const					{ return static_cast<T>(fValue); }
//	virtual uint32	Weight() const					{ return 1; }
//	
//	virtual uint32	Count() const					{ return fCount; }
//	virtual uint32	Read() const					{ return fRead; }
//
//  protected:
//					IteratorBase(CIBitStream& inData, int64 inMax, bool)
//						: fBits(&inData)
//						, fRead(0)
//						, fValue(-1)
//						, fMax(inMax)
//					{
//						int64 v = fMax;
//						fMaxWidth = 0;
//						while (v > 0)
//						{
//							v >>= 1;
//							++fMaxWidth;
//						}
//					}
//
//					IteratorBase(const IteratorBase& inOther);
//	IteratorBase&		operator=(const IteratorBase& inOther);
//
//	void			Reset()
//					{
//						fValue = -1;
//						ReadGamma(*fBits, fCount);
//						fRead = 0;
//						fSpan = 0;
//						fWidth = fMaxWidth;			// backwards compatible
//					}
//
//	CIBitStream*	fBits;
//	uint32			fCount;
//	uint32			fRead;
//	int32			fWidth;
//	uint32			fMaxWidth;
//	uint32			fSpan;
//
//	int64			fValue;
//	int64			fMax;
//};

// --------------------------------------------------------------------
//	Array Routines

void WriteArray(M6OBitStream& inBits, vector<uint32>& inArray)
{
	uint32 cnt = static_cast<uint32>(inArray.size());
	WriteGamma(inBits, cnt);
	assert(cnt == 0 or inArray.front() > 0);
	if (not inArray.empty() and inArray.front() == 0)
		THROW(("Invalid array, should not contain zero"));
	CompressSimpleArraySelector(inBits, inArray);
}

// --------------------------------------------------------------------
//	M6CompressedArray

M6CompressedArray::M6CompressedArray(const M6IBitStream& inBits, uint32 inLength)
	: mBits(inBits)
	, mSize(inLength)
{
}

M6CompressedArray::const_iterator::const_iterator()
	: mCount(0), mWidth(0), mSpan(0), mCurrent(0)
{
}

M6CompressedArray::const_iterator::const_iterator(const M6CompressedArray* inArray, const M6IBitStream& inBits, uint32 inLength)
	: mArray(inArray), mBits(inBits), mCount(inLength), mWidth(kMaxWidth), mSpan(0), mCurrent(0)
{
	operator++();
}

M6CompressedArray::const_iterator::const_iterator(const const_iterator& iter)
	: mBits(iter.mBits)
	, mCount(iter.mCount)
	, mWidth(iter.mWidth)
	, mSpan(iter.mSpan)
	, mCurrent(iter.mCurrent)
{
}

M6CompressedArray::const_iterator& M6CompressedArray::const_iterator::operator=(const const_iterator& iter)
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

M6CompressedArray::const_iterator& M6CompressedArray::const_iterator::operator++()
{
	if (mCount > 0)
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
	
	return *this;
}

M6CompressedArray::const_iterator M6CompressedArray::begin() const
{
	return const_iterator(this, mBits, mSize);
}

M6CompressedArray::const_iterator M6CompressedArray::end() const
{
	return const_iterator(this, mBits, 0);
}
