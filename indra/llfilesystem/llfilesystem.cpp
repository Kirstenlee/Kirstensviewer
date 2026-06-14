/**
 * @file filesystem.h
 * @brief Simulate local file system operations.
 * @Note The initial implementation does actually use standard C++
 *       file operations but eventually, there will be another
 *       layer that caches and manages file meta data too.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

// WARNING S24 Do not merge LL code
// This file differs significantly from the original implementation
// and is not intended to be merged back to the main codebase.
//
// It is a new implementation of the LLFileSystem class that uses
// C++17's std::filesystem library for file operations.
//
// This provides better performance and reliability compared to
// the original implementation that used C-style file operations.
//
// The new implementation also includes improvements such as:
// - Non-blocking file existence and size checks
// - More efficient way to update file access times

#include "linden_common.h"

#include "lldir.h"
#include "llfilesystem.h"
#include "llfasttimer.h"
#include "lldiskcache.h"

#include <filesystem> // S24 C++17 filesystem library

constexpr S32 LLFileSystem::READ        = 0x00000001;
constexpr S32 LLFileSystem::WRITE       = 0x00000002;
constexpr S32 LLFileSystem::READ_WRITE  = 0x00000003;  // LLFileSystem::READ & LLFileSystem::WRITE
constexpr S32 LLFileSystem::APPEND      = 0x00000006;  // 0x00000004 & LLFileSystem::WRITE

static LLTrace::BlockTimerStatHandle FTM_VFILE_WAIT("VFile Wait");

LLFileSystem::LLFileSystem(const LLUUID& file_id, const LLAssetType::EType file_type, S32 mode)
    : mFileType(file_type)
    , mFileID(file_id)
    , mPosition(0)
    , mBytesRead(0)
    , mMode(mode)
{
    // This block of code was originally called in the read() method but after comments here:
    // https://bitbucket.org/lindenlab/viewer/commits/e28c1b46e9944f0215a13cab8ee7dded88d7fc90#comment-10537114
    // we decided to follow Henri's suggestion and move the code to update the last access time here.
    if (mode == LLFileSystem::READ)
    {
        // build the filename (TODO: we do this in a few places - perhaps we should factor into a single function)
        const std::string filename = LLDiskCache::metaDataToFilepath(mFileID, mFileType);

        // update the last access time for the file if it exists - this is required
        // even though we are reading and not writing because this is the
        // way the cache works - it relies on a valid "last accessed time" for
        // each file so it knows how to remove the oldest, unused files
        std::error_code ec;
        if (std::filesystem::exists(filename, ec))
        {
            updateFileAccessTime(filename);
        }
    }
}

// S24 Non-blocking filesystem check
bool LLFileSystem::getExists(const LLUUID& file_id, LLAssetType::EType file_type)
{
    const std::string filename = LLDiskCache::metaDataToFilepath(file_id, file_type);

    std::error_code ec;
    return std::filesystem::exists(filename, ec) &&
           std::filesystem::is_regular_file(filename, ec) &&
           std::filesystem::file_size(filename, ec) > 0;
}

// static
bool LLFileSystem::removeFile(const LLUUID& file_id, const LLAssetType::EType file_type, int suppress_error /*= 0*/)
{
    const std::string filename = LLDiskCache::metaDataToFilepath(file_id, file_type);

    LLFile::remove(filename.c_str(), suppress_error);

    return true;
}

// static
bool LLFileSystem::renameFile(const LLUUID& old_file_id, const LLAssetType::EType old_file_type,
                              const LLUUID& new_file_id, const LLAssetType::EType new_file_type)
{
    const std::string old_filename = LLDiskCache::metaDataToFilepath(old_file_id, old_file_type);
    const std::string new_filename = LLDiskCache::metaDataToFilepath(new_file_id, new_file_type);

    if (LLFile::rename(old_filename, new_filename) != 0)
    {
        // We would like to return false here indicating the operation
        // failed but the original code does not and doing so seems to
        // break a lot of things so we go with the flow...
        //return false;
        LL_WARNS() << "Failed to rename " << old_file_id << " to " << new_file_id << " reason: " << strerror(errno) << LL_ENDL;
    }

    return true;
}

// S24 Non-blocking filesystem size check
S32 LLFileSystem::getFileSize(const LLUUID& file_id, LLAssetType::EType file_type)
{
    const std::string filename = LLDiskCache::metaDataToFilepath(file_id, file_type);

    std::error_code ec;
    if (std::filesystem::exists(filename, ec) &&
        std::filesystem::is_regular_file(filename, ec))
    {
        const auto size = std::filesystem::file_size(filename, ec);
        if (!ec)
        {
            return static_cast<S32>(size);
        }
    }

    return 0;
}

bool LLFileSystem::read(U8* buffer, S32 bytes)
{
    const std::string filename = LLDiskCache::metaDataToFilepath(mFileID, mFileType);

    llifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    file.seekg(mPosition, std::ios::beg);
    if (!file)
    {
        return false;
    }

    file.read(reinterpret_cast<char*>(buffer), bytes);

    mBytesRead = static_cast<S32>(file.gcount());
    mPosition += mBytesRead;

    return mBytesRead > 0;
}

S32 LLFileSystem::getLastBytesRead() const
{
    return mBytesRead;
}

bool LLFileSystem::eof() const
{
    return mPosition >= getSize();
}

bool LLFileSystem::write(const U8* buffer, S32 bytes)
{
    const std::string filename = LLDiskCache::metaDataToFilepath(mFileID, mFileType);

    if (mMode == APPEND)
    {
        llofstream ofs(filename, std::ios::app | std::ios::binary);
        if (ofs)
        {
            ofs.write(reinterpret_cast<const char*>(buffer), bytes);
            mPosition = static_cast<S32>(ofs.tellp());
            return true;
        }
    }
    else if (mMode == READ_WRITE)
    {
        // Check if file exists first to avoid unnecessary open attempts
        std::error_code ec;
        const bool file_exists = std::filesystem::exists(filename, ec);

        if (file_exists)
        {
            // Don't truncate if file already exists
            llofstream ofs(filename, std::ios::in | std::ios::out | std::ios::binary);
            if (ofs)
            {
                ofs.seekp(mPosition, std::ios::beg);
                ofs.write(reinterpret_cast<const char*>(buffer), bytes);
                mPosition += bytes;
                return true;
            }
        }
        else
        {
            // File doesn't exist - open in write mode
            llofstream ofs(filename, std::ios::binary);
            if (ofs)
            {
                ofs.write(reinterpret_cast<const char*>(buffer), bytes);
                mPosition += bytes;
                return true;
            }
        }
    }
    else
    {
        llofstream ofs(filename, std::ios::binary);
        if (ofs)
        {
            ofs.write(reinterpret_cast<const char*>(buffer), bytes);
            mPosition += bytes;
            return true;
        }
    }

    return false;
}

bool LLFileSystem::seek(S32 offset, S32 origin)
{
    if (-1 == origin)
    {
        origin = mPosition;
    }

    const S32 new_pos = origin + offset;
    const S32 size = getSize();

    if (new_pos > size)
    {
        LL_WARNS() << "Attempt to seek past end of file" << LL_ENDL;
        mPosition = size;
        return false;
    }
    else if (new_pos < 0)
    {
        LL_WARNS() << "Attempt to seek past beginning of file" << LL_ENDL;
        mPosition = 0;
        return false;
    }

    mPosition = new_pos;
    return true;
}

S32 LLFileSystem::tell() const
{
    return mPosition;
}

S32 LLFileSystem::getSize() const
{
    return LLFileSystem::getFileSize(mFileID, mFileType);
}

S32 LLFileSystem::getMaxSize() const
{
    // offer up a huge size since we don't care what the max is
    return INT_MAX;
}

bool LLFileSystem::rename(const LLUUID& new_id, const LLAssetType::EType new_type)
{
    LLFileSystem::renameFile(mFileID, mFileType, new_id, new_type);

    mFileID = new_id;
    mFileType = new_type;

    return true;
}

bool LLFileSystem::remove() const
{
    LLFileSystem::removeFile(mFileID, mFileType);
    return true;
}

void LLFileSystem::updateFileAccessTime(const std::string& file_path)
{
    /**
     * Threshold in time_t units that is used to decide if the last access time
     * time of the file is updated or not. Added as a precaution for the concern
     * outlined in SL-14582  about frequent writes on older SSDs reducing their
     * lifespan. I think this is the right place for the threshold value - rather
     * than it being a pref - do comment on that Jira if you disagree...
     *
     * Let's start with 1 hour in time_t units and see how that unfolds
     */
    constexpr std::time_t time_threshold = 1 * 60 * 60;

    std::error_code ec;

#if LL_WINDOWS
    const std::filesystem::path fs_path = ll_convert<std::wstring>(file_path);
#else
    const std::filesystem::path fs_path = file_path;
#endif

    // Get file last write time - non-throwing
    const auto last_write_file_time = std::filesystem::last_write_time(fs_path, ec);
    if (ec)
    {
        LL_WARNS() << "Failed to read last write time for cache file " << file_path << ": " << ec.message() << LL_ENDL;
        return;
    }

    // Convert file_time to time_t for comparison (C++17 compatible approach)
    // Calculate offset between file_clock and system_clock at current time
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        last_write_file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    const auto last_write_time = std::chrono::system_clock::to_time_t(sctp);
    const std::time_t cur_time = std::time(nullptr);

    // delta between cur time and last time the file was written
    const std::time_t delta_time = cur_time - last_write_time;

    // we only write the new value if the time in time_threshold has elapsed
    // before the last one
    if (delta_time > time_threshold)
    {
        const auto new_file_time = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(fs_path, new_file_time, ec);

        if (ec)
        {
            LL_WARNS() << "Failed to update last write time for cache file " << file_path << ": " << ec.message() << LL_ENDL;
        }
    }
}
