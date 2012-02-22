/*
	M6SortedRunArray
	
	When indexing files we build large arrays of entries that should
	eventually be sorted and then processed in order. These arrays can
	contain up to hundreds of millions of entries. Therefore, a simple
	vector is not the best solution.
	
	What we do here is in part based on an approach described in 'Managing
	Gigabytes'. We have an array (a run) of fixed size in which we write the
	entries one by one. When this array is full, we sort it and write it
	to disk and start with a new array. (In this implementation the sorting
	and writing is done in a separate thread).
	
	When we're finished filling the array, we call 'finish' and this
	returns an iterator object which can be used to retrieve the entries
	one by one in sorted order. This is accomplished by creating a
	priority queue of iterator objects for each run on disk.
	
	The HTempFileStream object has been extended to have a user defined
	number of cache blocks. To minimise disk head movement, we set this
	number to be at least the number of runs on disk.
	
*/

#pragma once

#include <list>
#include <vector>

#include <boost/thread.hpp>

#include "M6File.h"
#include "M6Queue.h"

template<class T>
class M6RunEntryWriter
{
  public:
	void		PrepareForWrite(const T* inValues, uint32 inCount) {}
	void		WriteSortedRun(M6File& inFile, const T* inValues, uint32 inCount)
					{ inFile.Write(inValues, sizeof(T) * inCount); }
};

template<class T>
class M6RunEntryReader
{
  public:
				M6RunEntryReader(M6File& inFile, int64 inOffset)
					: mFile(inFile)
					, mOffset(inOffset) { }

	void		ReadSortedRunEntry(T& inValue)		{ mFile.PRead(&inValue, sizeof(T), mOffset); mOffset += sizeof(T); }

	M6File&		mFile;
	int64		mOffset;
};

template
<
	class V,						// the value to store
	class C = std::greater<V>,		// comparator class, default is greater due to the way std::heap works
	uint32 N = 1000000,				// number of elements per buffer run, default is a million
	class W = M6RunEntryWriter<V>,	// IO helper classes
	class R = M6RunEntryReader<V>
>
class M6SortedRunArray
{
  public:

	typedef V	value_type;
	typedef C	compare_type;
	typedef W	writer_type;
	typedef R	reader_type;

  private:
	struct run_info
	{
		uint32	count;
		int64	offset;
	};
	
	typedef std::list<run_info>					M6RunInfoList;
	struct M6RunEntryIterator;
	typedef std::vector<M6RunEntryIterator*>	M6QueueType;
	
	struct thread_run_info
	{
		value_type*	run;
		uint32		run_count;
	};
	
	typedef M6Queue<thread_run_info,2>			M6ThreadRunQueue;
	
	struct M6CompareRunEntry
	{
						M6CompareRunEntry(compare_type&	comp)
							: comp(comp) {}
		
		bool			operator()(const M6RunEntryIterator* lhs, const M6RunEntryIterator* rhs) const
						{
							// NOTE: due to the way the STL heap functions work
							// we reverse the order of the arguments to compare here.
							return comp(rhs->mValue, lhs->mValue);
						}
		
		compare_type&	comp;
	};
	
  public:
	
				M6SortedRunArray(const boost::filesystem::path& inScratchFile, C inCompare = C(), W inWriter = W())
					: mFile(inScratchFile, eReadWrite)
					, mRun(nullptr)
					, mRunCount(0)
					, mComp(inCompare)
					, mFlushThread(boost::bind(&M6SortedRunArray::FlushRunThread, this))
					, mCount(0)
					, mWriter(inWriter)
				{
				}

				~M6SortedRunArray()
				{
					delete [] mRun;
				}

	void		PushBack(const value_type&	inValue)
				{
					if (mRunCount >= N)
						FlushRun();

					if (mRun == nullptr)
						mRun = new value_type[N];

					mRun[mRunCount] = inValue;
					++mRunCount;
					++mCount;
				}

	int64		Size() const				{ return mCount; }

	class iterator
	{
	  public:
					iterator(M6File& inFile, compare_type& inComp, M6RunInfoList& inRuns)
						: mFile(inFile)
						, mCompare(inComp)
					{
						for (typename M6RunInfoList::iterator r = inRuns.begin(); r != inRuns.end(); ++r)
						{
							std::auto_ptr<M6RunEntryIterator> re(new M6RunEntryIterator(mFile, r->offset, r->count));
							if (re->Next())
								mQueue.push_back(re.release());
						}
						std::make_heap(mQueue.begin(), mQueue.end(), mCompare);
					}

		bool		Next(value_type& v)
					{
						bool result = false;
						if (not mQueue.empty())
						{
							std::pop_heap(mQueue.begin(), mQueue.end(), mCompare);
							M6RunEntryIterator* r = mQueue.back();

							result = true;
							v = r->mValue;

							if (r->Next())
								std::push_heap(mQueue.begin(), mQueue.end(), mCompare);
							else
							{
								mQueue.erase(mQueue.end() - 1);
								delete r;
							}
						}
						return result;
					}

	  private:

					iterator(const iterator&);
		iterator&	operator=(const iterator&);
	
		M6File&				mFile;
		M6CompareRunEntry	mCompare;
		M6QueueType			mQueue;
	};
	
	iterator*	Finish()
				{
					if (mRunCount > 0 or mRun != nullptr)
						FlushRun();
					
					assert(mRun == nullptr);
					assert(mRunCount == 0);
					FlushRun();

					mFlushThread.join();

					return new iterator(mFile, mComp, mRuns);
				}

  private:
				M6SortedRunArray(const M6SortedRunArray&);
	M6SortedRunArray& operator=(const M6SortedRunArray&);

	void		FlushRun()
				{
					thread_run_info tri = { mRun, mRunCount };
					
					mFlushQueue.Put(tri);

					mRun = nullptr;
					mRunCount = 0;
				}

	void		FlushRunThread()
				{
					for (;;)
					{
						thread_run_info tri = mFlushQueue.Get();
						
						if (tri.run == nullptr)
							break;

						mWriter.PrepareForWrite(tri.run, tri.run_count);
	
						// don't use a quicksort here, it might take too much
						// stack space...
						stable_sort(tri.run, tri.run + tri.run_count, mComp);
						
						run_info ri;
						ri.offset = mFile.Tell();
						ri.count = tri.run_count;
						mRuns.push_back(ri);
						
						mWriter.WriteSortedRun(mFile, tri.run, tri.run_count);

						delete[] tri.run;
						tri.run = nullptr;
					}
				}
	
	struct M6RunEntryIterator
	{
						M6RunEntryIterator(M6File& inFile, int64 inOffset, uint32 inCount)
							: mReader(inFile, inOffset)
							, mCount(inCount) { }

		bool			Next()
						{
							bool result = false;
							if (mCount > 0)
							{
								mReader.ReadSortedRunEntry(mValue);
								result = true;
								
								--mCount;
							}
							return result;
						}
		
		reader_type		mReader;
		value_type		mValue;
		uint32			mCount;
	};

	M6File				mFile;
	value_type*			mRun;
	uint32				mRunCount;
	M6RunInfoList		mRuns;
	compare_type		mComp;
	M6ThreadRunQueue	mFlushQueue;
	boost::thread		mFlushThread;
	int64				mCount;
	writer_type			mWriter;
};
