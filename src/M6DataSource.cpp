#include "M6Lib.h"

#include <iostream>

#define LIBARCHIVE_STATIC
#include <archive.h>
#include <archive_entry.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "M6DataSource.h"
#include "M6Error.h"
#include "M6Progress.h"
#include "M6File.h"
#include "M6Exec.h"

#if defined(_MSC_VER)
#pragma comment(lib, "libarchive")
#pragma comment(lib, "libbz2")
#endif

using namespace std;
namespace fs = boost::filesystem;
namespace io = boost::iostreams;

// --------------------------------------------------------------------

struct M6DataSourceImpl
{
					M6DataSourceImpl(M6Progress& inProgress) : mProgress(inProgress) {}
	virtual			~M6DataSourceImpl() {}
	
	virtual M6DataSource::M6DataFile*
					Next() = 0;
	
	static M6DataSourceImpl*
					Create(const fs::path& inFile, M6Progress& inProgress);

	M6Progress&		mProgress;
};

// --------------------------------------------------------------------

M6DataSource::iterator::iterator(M6DataSourceImpl* inSource)
	: mSource(inSource), mDataFile(inSource->Next())
{
	if (mDataFile == nullptr)
		mSource = nullptr;
}

M6DataSource::iterator::iterator(const iterator& iter)
	: mSource(iter.mSource), mDataFile(iter.mDataFile)
{
	if (mDataFile != nullptr)
		++mDataFile->mRefCount;
}

M6DataSource::iterator&
M6DataSource::iterator::operator=(const iterator& iter)
{
	if (this != &iter)
	{
		if (mDataFile != nullptr and --mDataFile->mRefCount == 0)
			delete mDataFile;
		mSource = iter.mSource;
		mDataFile = iter.mDataFile;
		if (mDataFile != nullptr)
			++mDataFile->mRefCount;
	}
	return *this;
}

M6DataSource::iterator::~iterator()
{
	delete mDataFile;
}

M6DataSource::iterator& M6DataSource::iterator::operator++()
{
	assert(mSource);
	if (mDataFile != nullptr and --mDataFile->mRefCount == 0)
		delete mDataFile;
	mDataFile = mSource->Next();
	if (mDataFile == nullptr)
		mSource = nullptr;
	return *this;
}

// --------------------------------------------------------------------

struct M6PlainTextDataSourceImpl : public M6DataSourceImpl
{
					M6PlainTextDataSourceImpl(const fs::path& inFile, M6Progress& inProgress)
						: M6DataSourceImpl(inProgress), mFile(inFile) {}
	
	struct device : public io::source
	{
		typedef char			char_type;
		typedef io::source_tag	category;
	
						device(const fs::path inFile, M6Progress& inProgress)
							: mFile(inFile, eReadOnly), mProgress(inProgress) {}

		streamsize		read(char* s, streamsize n)
						{
							if (n > mFile.Size() - mFile.Tell())
								n = mFile.Size() - mFile.Tell();
							if (n > 0)
							{
								mFile.Read(s, n);
								mProgress.Consumed(n);
							}
							else
								n = -1;
							return n;
						}
	
		M6File			mFile;
		M6Progress&		mProgress;
	};

	virtual M6DataSource::M6DataFile*	Next()
					{
						M6DataSource::M6DataFile* result = nullptr;
						if (not mFile.empty())
						{
							result = new M6DataSource::M6DataFile;
							result->mFilename = mFile.filename().string();

							if (mFile.extension() == ".gz")
								result->mStream.push(io::gzip_decompressor());
							else if (mFile.extension() == ".bz2")
								result->mStream.push(io::bzip2_decompressor());
							
							result->mStream.push(device(mFile, mProgress));
							
							mProgress.Message(mFile.filename().string());
							mFile.clear();
						}
						return result;
					}

	fs::path		mFile;
};

// --------------------------------------------------------------------

struct M6ArchiveDataSourceImpl : public M6DataSourceImpl
{
	typedef M6DataSource::M6DataFile M6DataFile;
	
						M6ArchiveDataSourceImpl(struct archive* inArchive, M6Progress& inProgress)
							: M6DataSourceImpl(inProgress), mArchive(inArchive) {}
						~M6ArchiveDataSourceImpl();
	
	struct device : public io::source
	{
		typedef char			char_type;
		typedef io::source_tag	category;
	
						device(struct archive* inArchive, struct archive_entry* inEntry,
								M6Progress& inProgress)
							: mArchive(inArchive), mEntry(inEntry), mProgress(inProgress)
							, mLastPosition(archive_position_compressed(mArchive))
						{
						}

		streamsize		read(char* s, streamsize n)
						{
							streamsize result = -1;
							if (mEntry != nullptr)
							{
								result = archive_read_data(mArchive, s, n);
								if (result == 0)
									mEntry = nullptr;
							}
							
							if (result >= 0)
							{
								int64 p = archive_position_compressed(mArchive);
								mProgress.Consumed(p - mLastPosition);
								mLastPosition = p;
							}

							return result;
						}
	
		struct archive*	mArchive;
		struct archive_entry*
						mEntry;
		M6Progress&		mProgress;
		int64			mLastPosition;
	};

	virtual M6DataFile*	Next()
					{
						M6DataFile* result = nullptr;

						if (mArchive != nullptr)
						{
							for (;;)
							{
								archive_entry* entry;
								if (archive_read_next_header(mArchive, &entry) != ARCHIVE_OK)
									THROW((archive_error_string(mArchive)));
								
								// skip over non files
								if (archive_entry_filetype(entry) != AE_IFREG)
									continue;
								
								result = new M6DataFile;
								const char* path = archive_entry_pathname(entry);
								result->mFilename = path;
								result->mStream.push(device(mArchive, entry, mProgress));
								mProgress.Message(path);
								
								break;
							}
						}
						
						return result;
					}

	struct archive*	mArchive;
};

M6ArchiveDataSourceImpl::~M6ArchiveDataSourceImpl()
{
	if (mArchive != nullptr)
		archive_read_finish(mArchive);
}

// --------------------------------------------------------------------

M6DataSourceImpl* M6DataSourceImpl::Create(const fs::path& inFile, M6Progress& inProgress)
{
	M6DataSourceImpl* result = nullptr;

	const uint32 kBufferSize = 4096;
	
	struct archive* archive = archive_read_new();
	
	if (archive == nullptr)
		THROW(("Failed to initialize libarchive"));
	
	int err = archive_read_support_compression_all(archive);

	if (err == ARCHIVE_OK)
		err = archive_read_support_format_all(archive);
	
	if (err == ARCHIVE_OK)
		err = archive_read_open_filename(archive, inFile.string().c_str(), kBufferSize);
	
	if (err == ARCHIVE_OK)	// a supported archive
		result = new M6ArchiveDataSourceImpl(archive, inProgress);
	else
	{
		if (VERBOSE)
			cerr << archive_error_string(archive) << endl;

		archive_read_finish(archive);
		result = new M6PlainTextDataSourceImpl(inFile, inProgress);
	}
	
	return result;
}

// --------------------------------------------------------------------

M6DataSource::M6DataSource(const fs::path& inFile, M6Progress& inProgress)
	: mImpl(M6DataSourceImpl::Create(inFile, inProgress))
{
}

M6DataSource::~M6DataSource()
{
	delete mImpl;
}
