/**
 * @file llfile.cpp
 * @author Michael Schlachter
 * @date 2006-03-23
 * @brief Implementation of cross-platform POSIX file buffer and c++
 * stream classes.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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
 
#include "llwin32headers.h"
#include <stdlib.h>                 // Windows errno
#include <vector>

#include "linden_common.h"
#include "llfile.h"
#include "llstring.h"
#include "llerror.h"
#include "stringize.h"


using namespace std;

static std::string empty;

// For the situations where we directly call into Windows API functions we need to translate
// the Windows error codes into errno values
namespace
{
    struct errentry
    {
        unsigned long oserr; // Windows OS error value
        int errcode;         // System V error code
    };
}

// translation table between Windows OS error value and System V errno code
static errentry const errtable[]
{
    { ERROR_INVALID_FUNCTION,       EINVAL    },  //    1
    { ERROR_FILE_NOT_FOUND,         ENOENT    },  //    2
    { ERROR_PATH_NOT_FOUND,         ENOENT    },  //    3
    { ERROR_TOO_MANY_OPEN_FILES,    EMFILE    },  //    4
    { ERROR_ACCESS_DENIED,          EACCES    },  //    5
    { ERROR_INVALID_HANDLE,         EBADF     },  //    6
    { ERROR_ARENA_TRASHED,          ENOMEM    },  //    7
    { ERROR_NOT_ENOUGH_MEMORY,      ENOMEM    },  //    8
    { ERROR_INVALID_BLOCK,          ENOMEM    },  //    9
    { ERROR_BAD_ENVIRONMENT,        E2BIG     },  //   10
    { ERROR_BAD_FORMAT,             ENOEXEC   },  //   11
    { ERROR_INVALID_ACCESS,         EINVAL    },  //   12
    { ERROR_INVALID_DATA,           EINVAL    },  //   13
    { ERROR_INVALID_DRIVE,          ENOENT    },  //   15
    { ERROR_CURRENT_DIRECTORY,      EACCES    },  //   16
    { ERROR_NOT_SAME_DEVICE,        EXDEV     },  //   17
    { ERROR_NO_MORE_FILES,          ENOENT    },  //   18
    { ERROR_LOCK_VIOLATION,         EACCES    },  //   33
    { ERROR_BAD_NETPATH,            ENOENT    },  //   53
    { ERROR_NETWORK_ACCESS_DENIED,  EACCES    },  //   65
    { ERROR_BAD_NET_NAME,           ENOENT    },  //   67
    { ERROR_FILE_EXISTS,            EEXIST    },  //   80
    { ERROR_CANNOT_MAKE,            EACCES    },  //   82
    { ERROR_FAIL_I24,               EACCES    },  //   83
    { ERROR_INVALID_PARAMETER,      EINVAL    },  //   87
    { ERROR_NO_PROC_SLOTS,          EAGAIN    },  //   89
    { ERROR_DRIVE_LOCKED,           EACCES    },  //  108
    { ERROR_BROKEN_PIPE,            EPIPE     },  //  109
    { ERROR_DISK_FULL,              ENOSPC    },  //  112
    { ERROR_INVALID_TARGET_HANDLE,  EBADF     },  //  114
    { ERROR_WAIT_NO_CHILDREN,       ECHILD    },  //  128
    { ERROR_CHILD_NOT_COMPLETE,     ECHILD    },  //  129
    { ERROR_DIRECT_ACCESS_HANDLE,   EBADF     },  //  130
    { ERROR_NEGATIVE_SEEK,          EINVAL    },  //  131
    { ERROR_SEEK_ON_DEVICE,         EACCES    },  //  132
    { ERROR_DIR_NOT_EMPTY,          ENOTEMPTY },  //  145
    { ERROR_NOT_LOCKED,             EACCES    },  //  158
    { ERROR_BAD_PATHNAME,           ENOENT    },  //  161
    { ERROR_MAX_THRDS_REACHED,      EAGAIN    },  //  164
    { ERROR_LOCK_FAILED,            EACCES    },  //  167
    { ERROR_ALREADY_EXISTS,         EEXIST    },  //  183
    { ERROR_FILENAME_EXCED_RANGE,   ENOENT    },  //  206
    { ERROR_NESTING_NOT_ALLOWED,    EAGAIN    },  //  215
    { ERROR_NO_UNICODE_TRANSLATION, EILSEQ    },  // 1113
    { ERROR_NOT_ENOUGH_QUOTA,       ENOMEM    }   // 1816
};

static int set_errno_from_oserror(unsigned long oserr)
{
    if (!oserr)
        return 0;

    // Check the table for the Windows OS error code
    for (const struct errentry &entry : errtable)
    {
        if (oserr == entry.oserr)
        {
            _set_errno(entry.errcode);
            return -1;
        }
    }

    _set_errno(EINVAL);
    return -1;
}

// On Windows, use strerror_s().
std::string strerr(int errn)
{
	char buffer[256];
	strerror_s(buffer, errn);       // infers sizeof(buffer) -- love it!
	return buffer;
}

inline bool is_slash(wchar_t const c)
{
    return c == L'\\' || c == L'/';
}

static std::wstring utf8path_to_wstring(const std::string& utf8path)
{
    if (utf8path.size() >= MAX_PATH)
    {
        // By prepending "\\?\" to a path, Windows widechar file APIs will not fail on long path names
        std::wstring utf16path = L"\\\\?\\" + ll_convert<std::wstring>(utf8path);
        // We need to make sure that the path does not contain forward slashes as above
        // prefix does bypass the path normalization that replaces slashes with backslashes
        // before passing the path to kernel mode APIs
        std::replace(utf16path.begin(), utf16path.end(), L'/', L'\\');
        return utf16path;
    }
    return ll_convert<std::wstring>(utf8path);
}

static unsigned short get_fileattr(const std::wstring& utf16path, bool dontFollowSymLink = false)
{
    unsigned long  flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (dontFollowSymLink)
    {
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    }
    HANDLE file_handle = CreateFileW(utf16path.c_str(), FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, flags, nullptr);
    if (file_handle != INVALID_HANDLE_VALUE)
    {
        FILE_ATTRIBUTE_TAG_INFO attribute_info;
        if (GetFileInformationByHandleEx(file_handle, FileAttributeTagInfo, &attribute_info, sizeof(attribute_info)))
        {
            // A volume path alone (only drive letter) is not recognized as directory while it technically is
            bool is_directory = (attribute_info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                                (iswalpha(utf16path[0]) && utf16path[1] == ':' &&
                                 (!utf16path[2] || (is_slash(utf16path[2]) && !utf16path[3])));
            unsigned short st_mode = is_directory ? S_IFDIR :
                                     (attribute_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? S_IFLNK : S_IFREG);
            st_mode |= (attribute_info.FileAttributes & FILE_ATTRIBUTE_READONLY) ? S_IREAD : S_IREAD | S_IWRITE;
            // we do not try to guess executable flag

            // propagate user bits to group/other fields:
            st_mode |= (st_mode & 0700) >> 3;
            st_mode |= (st_mode & 0700) >> 6;

            CloseHandle(file_handle);
            return st_mode;
        }
    }
    // Retrieve last error and set errno before calling CloseHandle()
    set_errno_from_oserror(GetLastError());

    if (file_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file_handle);
    }
    return 0;
}



// On either system, shorthand call just infers global 'errno'.
std::string strerr()
{
	return strerr(errno);
}

static int warnif(const std::string& desc, const std::string& filename, int rc, int accept = 0)
{
	if (rc < 0)
	{
		// Capture errno before we start emitting output
		int errn = errno;

		// For certain operations, a particular errno value might be
		// acceptable -- e.g. stat() could permit ENOENT, mkdir() could permit
		// EEXIST. Don't warn if caller explicitly says this errno is okay.
		if (errn != accept)
		{
			LL_WARNS("LLFile") << "Couldn't " << desc << " '" << filename
							   << "' (errno " << errn << "): " << strerr(errn) << LL_ENDL;
		}
	}
	return rc;
}

// static
int	LLFile::mkdir(const std::string& dirname, int perms)
{
    // We often use mkdir() to ensure the existence of a directory that might
    // already exist. There is no known case in which we want to call out as
    // an error the requested directory already existing.
	// permissions are ignored on Windows
    int rc = 0;
    std::wstring utf16dirname = utf8path_to_wstring(dirname);
    if (!CreateDirectoryW(utf16dirname.c_str(), nullptr))
    {
	// already exist. There is no known case in which we want to call out as
        unsigned long oserr = GetLastError();
        if (oserr != ERROR_ALREADY_EXISTS)
        {
            rc = set_errno_from_oserror(oserr);
        }
    }
	// anything else might be a problem
    return warnif("mkdir", dirname, rc);
}

// static
int LLFile::rmdir(const std::string& dirname, int suppress_error)
{

    std::wstring utf16dirname = utf8path_to_wstring(dirname);
	int rc = _wrmdir(utf16dirname.c_str());

    return warnif("rmdir", dirname, rc, suppress_error);
}

//----------------------------------------------------------------------------------------
// LLFile instance (RAII file handle) member functions
//----------------------------------------------------------------------------------------
namespace
{
    inline int set_ec_from_system_error(std::error_code& ec, DWORD error)
    {
        ec.assign(error, std::system_category());
        return -1;
    }

    int set_ec_from_system_error(std::error_code& ec)
    {
        return set_ec_from_system_error(ec, GetLastError());
    }

    inline int set_ec_to_parameter_error(std::error_code& ec)
    {
        return set_ec_from_system_error(ec, ERROR_INVALID_PARAMETER);
    }

    inline DWORD decode_access_mode(std::ios_base::openmode omode)
    {
        switch (omode & (LLFile::in | LLFile::out))
        {
            case LLFile::in:
                return GENERIC_READ;
            case LLFile::out:
                return GENERIC_WRITE;
            case static_cast<std::ios_base::openmode>(LLFile::in | LLFile::out):
                return GENERIC_READ | GENERIC_WRITE;
        }
        if (omode & LLFile::app)
        {
            return GENERIC_WRITE;
        }
        return 0;
    }

    inline DWORD decode_open_create_flags(std::ios_base::openmode omode)
    {
        if (omode & LLFile::noreplace)
        {
            return CREATE_NEW; // create if it does not exist, otherwise fail
        }
        if (omode & LLFile::trunc)
        {
            if (!(omode & LLFile::out))
            {
                return TRUNCATE_EXISTING; // open and truncate if it exists, otherwise fail
            }
            return CREATE_ALWAYS; // open and truncate if it exists, otherwise create it
        }
        if (!(omode & LLFile::out))
        {
            return OPEN_EXISTING; // open if it exists, otherwise fail
        }
        // LLFile::app or (LLFile::out and (!LLFile::trunc or !LLFile::noreplace))
        return OPEN_ALWAYS; // open if it exists, otherwise create it
    }

    inline DWORD decode_share_mode(int omode)
    {
        if (omode & LLFile::exclusive)
        {
            return 0; // allow no other access
        }
        if (omode & LLFile::shared)
        {
            return FILE_SHARE_READ; // allow read access
        }
        return FILE_SHARE_READ | FILE_SHARE_WRITE; // allow read and write access to others
    }

    inline DWORD decode_attributes(std::ios_base::openmode omode, int perm)
    {
        return (perm & S_IWRITE) ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_READONLY;
    }

    DWORD seek_mode_from_dir(std::ios_base::seekdir seekdir)
    {
        switch (seekdir)
        {
            case LLFile::beg:
                return FILE_BEGIN;
            case LLFile::cur:
                return FILE_CURRENT;
            case LLFile::end:
                return FILE_END;
        }
        return FILE_BEGIN;
    }

    inline int clear_error(std::error_code& ec)
    {
        ec.clear();
        return 0;
    }

    inline bool are_open_mode_flags_invalid(std::ios_base::openmode omode)
    {
        // at least one of input or output needs to be specified
        if (!(omode & (LLFile::in | LLFile::out)))
        {
            return true;
        }
        // output must be possible for any of the extra options
        if (!(omode & LLFile::out) && (omode & (LLFile::trunc | LLFile::app | LLFile::noreplace)))
        {
            return true;
        }
        // invalid combination, mutually exclusive
        if ((omode & LLFile::app) && (omode & (LLFile::trunc | LLFile::noreplace)))
        {
            return true;
        }
        return false;
    }

    inline DWORD next_buffer_size(S64 nbytes)
    {
        return nbytes > 0x80000000 ? 0x80000000 : (DWORD)nbytes;
    }
}

int LLFile::open(const std::filesystem::path& file_path, std::ios_base::openmode omode, std::error_code& ec, int perm)
{
    close(ec);
    if (are_open_mode_flags_invalid(omode))
    {
        return set_ec_to_parameter_error(ec);
    }

    DWORD access = decode_access_mode(omode),
          share = decode_share_mode(omode),
          create = decode_open_create_flags(omode),
          attributes = decode_attributes(omode, perm);

    mHandle = (void*)CreateFileW(file_path.native().c_str(), access, share, nullptr, create, attributes, nullptr);
    // The dwShareMode = share parameter takes care of locking the file for other processes if indicated,
    // no need to do anything else for file locking here

    if (mHandle == InvalidHandle)
    {
        return set_ec_from_system_error(ec);
    }

    if (omode & LLFile::ate && seek(0, LLFile::end, ec) != 0)
    {
        close();
        return -1;
    }
    mOpen = omode;
    return clear_error(ec);
}

S64 LLFile::size(std::error_code& ec)
{
    LARGE_INTEGER value = { 0 };
    if (GetFileSizeEx((HANDLE)mHandle, &value))
    {
        clear_error(ec);
        return value.QuadPart;
    }
    set_ec_from_system_error(ec);
    return 0;
}

S64 LLFile::tell(std::error_code& ec)
{
    LARGE_INTEGER value = { 0 };
    if (SetFilePointerEx((HANDLE)mHandle, value, &value, FILE_CURRENT))
    {
        clear_error(ec);
        return value.QuadPart;
    }
    return set_ec_from_system_error(ec);
}

int LLFile::seek(S64 pos, std::error_code& ec)
{
    return seek(pos, LLFile::beg, ec);
}

int LLFile::seek(S64 offset, std::ios_base::seekdir dir, std::error_code& ec)
{
    S64 newOffset = 0;
    DWORD seekdir = seek_mode_from_dir(dir);
    LARGE_INTEGER value;
    value.QuadPart = offset;
    if (SetFilePointerEx((HANDLE)mHandle, value, (PLARGE_INTEGER)&newOffset, seekdir))
    {
        return clear_error(ec);
    }
    return set_ec_from_system_error(ec);
}

S64 LLFile::read(void* buffer, S64 nbytes, std::error_code& ec)
{
    if (nbytes == 0)
    {
        return clear_error(ec);
    }
    else if (!buffer || nbytes < 0)
    {
        return set_ec_to_parameter_error(ec);
    }

    S64 totalBytes = 0;
    char *ptr = (char*)buffer;
    DWORD bytesRead, bytesToRead = next_buffer_size(nbytes);

    // Read in chunks to support >4GB which the S64 nbytes value makes possible
    while (ReadFile((HANDLE)mHandle, ptr, bytesToRead, &bytesRead, nullptr))
    {
        totalBytes += bytesRead;
        if (nbytes <= totalBytes || // requested amount read
            bytesRead < bytesToRead) // ReadFile encountered eof
        {
            clear_error(ec);
            return totalBytes;
        }
        ptr += bytesRead;
        bytesToRead = next_buffer_size(nbytes - totalBytes);
    }
    return set_ec_from_system_error(ec);
}

S64 LLFile::write(const void* buffer, S64 nbytes, std::error_code& ec)
{
    if (nbytes == 0)
    {
        return clear_error(ec);
    }
    else if (!buffer || nbytes < 0)
    {
        return set_ec_to_parameter_error(ec);
    }

    // If this was opened in append mode, we emulate it on Windows
    if (mOpen & LLFile::app && seek(0, LLFile::end, ec) != 0)
    {
        return -1;
    }

    S64 totalBytes = 0;
    char* ptr = (char*)buffer;
    DWORD bytesWritten, bytesToWrite = next_buffer_size(nbytes);

    // Write in chunks to support >4GB which the S64 nbytes value makes possible
    while (WriteFile((HANDLE)mHandle, ptr, bytesToWrite, &bytesWritten, nullptr))
    {
        totalBytes += bytesWritten;
        if (nbytes <= totalBytes)
        {
            clear_error(ec);
            return totalBytes;
        }
        ptr += bytesWritten;
        bytesToWrite = next_buffer_size(nbytes - totalBytes);
    }
    return set_ec_from_system_error(ec);
}

int LLFile::close(std::error_code& ec)
{
    if (mHandle != InvalidHandle)
    {
        llfile_handle_t handle = InvalidHandle;
        std::swap(handle, mHandle);
        if (!CloseHandle((HANDLE)handle))
        {
            return set_ec_from_system_error(ec);
        }
    }
    return clear_error(ec);
}

int LLFile::close()
{
    std::error_code ec;
    return close(ec);
}

//----------------------------------------------------------------------------------------
// static member functions (original S24 LLFile API, unchanged)
//----------------------------------------------------------------------------------------

// static
LLFILE*	LLFile::fopen(const std::string& filename, const char* mode)	/* Flawfinder: ignore */
{

    std::wstring utf16filename = utf8path_to_wstring(filename);
    std::wstring utf16mode = ll_convert<std::wstring>(std::string(mode));
	return _wfopen(utf16filename.c_str(),utf16mode.c_str());

}

// static
int	LLFile::close(LLFILE * file)
{
	int ret_value = 0;
	if (file)
	{
		ret_value = fclose(file);
	}
	return ret_value;
}

// static
std::string LLFile::getContents(const std::string& filename)
{
    LLFILE* fp = LLFile::fopen(filename, "rb");
    if (fp)
    {
        fseek(fp, 0, SEEK_END);
        U32 length = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        std::vector<char> buffer(length);
        size_t nread = fread(buffer.data(), 1, length, fp);
        fclose(fp);

        return std::string(buffer.data(), nread);
    }

    return LLStringUtil::null;
}

// static
int LLFile::remove(const std::string& filename, int suppress_error)
{
    // Posix remove() works on both files and directories although on Windows
    // remove() and its wide char variant _wremove() only removes files just
    // as its siblings unlink() and _wunlink().
    // If we really only want to support files we should instead use
    // unlink() in the non-Windows part below too
    int rc = -1;
    std::wstring utf16filename = utf8path_to_wstring(filename);
    unsigned short st_mode = get_fileattr(utf16filename);
    if (S_ISDIR(st_mode))
    {
        rc = _wrmdir(utf16filename.c_str());
    }
    else if (S_ISREG(st_mode))
    {
        rc = _wunlink(utf16filename.c_str());
    }
    else if (st_mode)
    {
        // it is something else than a file or directory
        // this should not really happen as long as we do not allow for symlink
        // detection in the optional parameter to get_fileattr()
        rc = set_errno_from_oserror(ERROR_INVALID_PARAMETER);
    }
    else
    {
        // get_fileattr() failed and already set errno, preserve it for correct error reporting
    }
    return warnif("remove", filename, rc, suppress_error);
}

// static
int LLFile::rename(const std::string& filename, const std::string& newname, int suppress_error)
{

    // Posix rename() will gladly overwrite a file at newname if it exists, the Windows
    // rename(), respectively _wrename(), will bark on that. Instead call directly the Windows
    // API MoveFileEx() and use its flags to specify that overwrite is allowed.
    std::wstring utf16filename = utf8path_to_wstring(filename);
    std::wstring utf16newname = utf8path_to_wstring(newname);
    int rc = 0;
    if (!MoveFileExW(utf16filename.c_str(), utf16newname.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    {
        rc = set_errno_from_oserror(GetLastError());
    }

    return warnif(STRINGIZE("rename to '" << newname << "' from"), filename, rc, suppress_error);
}

// Make this a define rather than using magic numbers multiple times in the code
#define LLFILE_COPY_BUFFER_SIZE 16384

// static
bool LLFile::copy(const std::string& from, const std::string& to)
{
	bool copied = false;
	LLFILE* in = LLFile::fopen(from, "rb");		/* Flawfinder: ignore */	 	
	if (in)	 	
	{	 	
		LLFILE* out = LLFile::fopen(to, "wb");		/* Flawfinder: ignore */
		if (out)
		{
            char buf[LLFILE_COPY_BUFFER_SIZE];
			size_t readbytes;
			bool write_ok = true;
            while (write_ok && (readbytes = fread(buf, 1, LLFILE_COPY_BUFFER_SIZE, in)))
			{
				if (fwrite(buf, 1, readbytes, out) != readbytes)
				{
					LL_WARNS("LLFile") << "Short write" << LL_ENDL; 
					write_ok = false;
				}
			}
			if ( write_ok )
			{
				copied = true;
			}
			fclose(out);
		}
		fclose(in);
	}
	return copied;
}

// static
int LLFile::stat(const std::string& filename, llstat* filestatus, int suppress_error)
{

    std::wstring utf16filename = utf8path_to_wstring(filename);
    int rc = _wstat64(utf16filename.c_str(), filestatus);
	
    return warnif("stat", filename, rc, suppress_error);
}

// static
unsigned short LLFile::getattr(const std::string& filename, bool dontFollowSymLink, int suppress_error)
{
	// Don't spam the log if the subject pathname doesn't exist.
    int rc = -1;
    std::wstring utf16filename = utf8path_to_wstring(filename);
    unsigned short st_mode = get_fileattr(utf16filename, dontFollowSymLink);
    if (st_mode)
    {
        return st_mode;
    }
    warnif("getattr", filename, rc, suppress_error);
    return 0;
}

// static
bool LLFile::isdir(const std::string& filename)
{
    return S_ISDIR(getattr(filename));
}

// static
bool LLFile::isfile(const std::string& filename)
{
    return S_ISREG(getattr(filename));
}

// static
bool LLFile::islink(const std::string& filename)
{
    return S_ISLNK(getattr(filename, true));
}

// static
const char *LLFile::tmpdir()
{
	static std::string utf8path;

	if (utf8path.empty())
	{
		char sep;

		sep = '\\';

		std::vector<wchar_t> utf16path(MAX_PATH + 1);
        GetTempPathW(static_cast<DWORD>(utf16path.size()), &utf16path[0]);
		utf8path = ll_convert_wide_to_string(&utf16path[0]);

		if (utf8path[utf8path.size() - 1] != sep)
		{
			utf8path += sep;
		}
	}
	return utf8path.c_str();
}



/************** input file stream ********************************/

llifstream::llifstream() {}

// explicit
llifstream::llifstream(const std::string& _Filename, ios_base::openmode _Mode):
    std::ifstream(ll_convert<std::wstring>( _Filename ).c_str(),
                  _Mode | ios_base::in)
{
}

void llifstream::open(const std::string& _Filename, ios_base::openmode _Mode)
{
    std::ifstream::open(ll_convert<std::wstring>(_Filename).c_str(),
                        _Mode | ios_base::in);
}


/************** output file stream ********************************/


llofstream::llofstream() {}

// explicit
llofstream::llofstream(const std::string& _Filename, ios_base::openmode _Mode):
    std::ofstream(ll_convert<std::wstring>( _Filename ).c_str(),
                  _Mode | ios_base::out)
{
}

void llofstream::open(const std::string& _Filename, ios_base::openmode _Mode)
{
    std::ofstream::open(ll_convert<std::wstring>( _Filename ).c_str(),
                        _Mode | ios_base::out);
}

/************** helper functions ********************************/

std::streamsize llifstream_size(llifstream& ifstr)
{
	if(!ifstr.is_open()) return 0;
	std::streampos pos_old = ifstr.tellg();
	ifstr.seekg(0, ios_base::beg);
	std::streampos pos_beg = ifstr.tellg();
	ifstr.seekg(0, ios_base::end);
	std::streampos pos_end = ifstr.tellg();
	ifstr.seekg(pos_old, ios_base::beg);
	return pos_end - pos_beg;
}

std::streamsize llofstream_size(llofstream& ofstr)
{
	if(!ofstr.is_open()) return 0;
	std::streampos pos_old = ofstr.tellp();
	ofstr.seekp(0, ios_base::beg);
	std::streampos pos_beg = ofstr.tellp();
	ofstr.seekp(0, ios_base::end);
	std::streampos pos_end = ofstr.tellp();
	ofstr.seekp(pos_old, ios_base::beg);
	return pos_end - pos_beg;
}

