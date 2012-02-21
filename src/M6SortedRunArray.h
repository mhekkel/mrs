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
#include <boost/filesystem/path.hpp>

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
				M6RunEntryReader(M6File& inFile)
					: mFile(inFile)
					, mOffset(0) { }

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
	struct M6RunInfo
	{
		uint32	count;
		M6File	file;
	};
	
	typedef std::list<M6RunInfo>				M6RunInfoList;
	struct M6RunEntryIterator;
	typedef std::vector<M6RunEntryIterator*>	M6QueueType;
	
	struct M6ThreadRunInfo
	{
		value_type*	run;
		uint32		run_count;
	};
	
	typedef M6Queue<M6ThreadRunInfo,8>			M6ThreadRunQueue;
	
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
					: mPath(inScratchFile)
					, mRun(nullptr)
					, mRunCount(0)
					, mComp(inCompare)
					, mCount(0)
					, mWriter(inWriter)
				{
					for (int i = 0; i < 4; ++i)
						mFlushThreads.create_thread(boost::bind(&M6SortedRunArray::FlushRunThread, this));
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
					iterator(compare_type& inComp, M6RunInfoList& inRuns)
						: mCompare(inComp)
					{
						for (typename M6RunInfoList::iterator r = inRuns.begin(); r != inRuns.end(); ++r)
						{
							std::auto_ptr<M6RunEntryIterator> re(new M6RunEntryIterator(r->file, r->count));
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

					mFlushThreads.join_all();

					return new iterator(mComp, mRuns);
				}

  private:
				M6SortedRunArray(const M6SortedRunArray&);
	M6SortedRunArray& operator=(const M6SortedRunArray&);

	void		FlushRun()
				{
					M6ThreadRunInfo tri = { mRun, mRunCount };
					
					mFlushQueue.Put(tri);

					mRun = nullptr;
					mRunCount = 0;
				}

	void		FlushRunThread()
				{
					for (;;)
					{
						M6ThreadRunInfo tri = mFlushQueue.Get();
						
						if (tri.run == nullptr)
							break;

						mWriter.PrepareForWrite(tri.run, tri.run_count);
	
						// don't use a quicksort here, it might take too much
						// stack space...
						stable_sort(tri.run, tri.run + tri.run_count, mComp);
						
						M6File file(mPath.parent_path() / (mPath.filename().string() + '.' + boost::lexical_cast<string>(mRuns.size())), eReadWrite);
						mWriter.WriteSortedRun(file, tri.run, tri.run_count);
						
						M6RunInfo ri = { tri.run_count, file };
						mRuns.push_back(ri);

						delete[] tri.run;
						tri.run = nullptr;
					}

					M6ThreadRunInfo sentinel = {};
					mFlushQueue.Put(sentinel);
				}
	
	struct M6RunEntryIterator
	{
						M6RunEntryIterator(M6File& inFile, uint32 inCount)
							: mReader(inFile, 0)
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

	boost::filesystem::path
						mPath;
	value_type*			mRun;
	uint32				mRunCount;
	M6RunInfoList		mRuns;
	compare_type		mComp;
	M6ThreadRunQueue	mFlushQueue;
	boost::thread_group	mFlushThreads;
	int64				mCount;
	writer_type			mWriter;
};
