#include "M6Lib.h"

#include <boost/pool/pool.hpp>
#include <boost/thread/tss.hpp>

#define PCRE_STATIC
#include <pcre.h>

// --------------------------------------------------------------------

#if defined(_MSC_VER)

class M6PCREStackPool
{
  public:
					M6PCREStackPool();
					~M6PCREStackPool();
  private:
	
	static void*	malloc(size_t inSize)			{ return sInstance.malloc_from_pool(inSize); }
	static void		free(void* inPtr)				{ sInstance.free_from_pool(inPtr); }

	void*			malloc_from_pool(size_t inSize);
	void			free_from_pool(void* inPtr);

	typedef boost::pool<boost::default_user_allocator_malloc_free>	M6Pool;
	//typedef boost::thread_specific_ptr<M6Pool>					M6PoolPtr;

	static M6PCREStackPool	sInstance;
	M6Pool*		 			mPool;
	boost::mutex			mMutex;
};

M6PCREStackPool::M6PCREStackPool()
	: mPool(nullptr)
{
	pcre_stack_malloc = &M6PCREStackPool::malloc;
	pcre_stack_free = &M6PCREStackPool::free;
}

M6PCREStackPool::~M6PCREStackPool()
{
	pcre_stack_malloc = pcre_malloc;
	pcre_stack_free = pcre_free;

	delete mPool;
}

void* M6PCREStackPool::malloc_from_pool(size_t inSize)
{
	boost::mutex::scoped_lock lock(mMutex);

	if (mPool == nullptr)	// first call
		mPool = new M6Pool(inSize);

	return mPool->malloc();
}

void M6PCREStackPool::free_from_pool(void* inPtr)
{
	boost::mutex::scoped_lock lock(mMutex);
	mPool->free(inPtr);
}

M6PCREStackPool M6PCREStackPool::sInstance;

#else

class M6PCREStackPool
{
  public:
					M6PCREStackPool();
					~M6PCREStackPool();
  private:
	
	static void*	malloc(size_t inSize)			{ return sInstance.malloc_from_pool(inSize); }
	static void		free(void* inPtr)				{ sInstance.free_from_pool(inPtr); }

	void*			malloc_from_pool(size_t inSize);
	void			free_from_pool(void* inPtr);

	typedef boost::pool<boost::default_user_allocator_malloc_free>	M6Pool;
	typedef boost::thread_specific_ptr<M6Pool>						M6PoolPtr;

	static M6PCREStackPool	sInstance;
	M6PoolPtr	 			mPool;
};

M6PCREStackPool::M6PCREStackPool()
{
	pcre_stack_malloc = &M6PCREStackPool::malloc;
	pcre_stack_free = &M6PCREStackPool::free;
}

M6PCREStackPool::~M6PCREStackPool()
{
	pcre_stack_malloc = pcre_malloc;
	pcre_stack_free = pcre_free;
}

void* M6PCREStackPool::malloc_from_pool(size_t inSize)
{
	if (mPool.get() == nullptr)	// first call
		mPool.reset(new M6Pool(inSize));

	return mPool->malloc();
}

void M6PCREStackPool::free_from_pool(void* inPtr)
{
	mPool->free(inPtr);
}

M6PCREStackPool M6PCREStackPool::sInstance;

#endif
