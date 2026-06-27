/**
 * @file llsys.cpp
 * @brief Implementation of the basic system query functions.
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

#if LL_WINDOWS
#pragma warning (disable : 4355) // 'this' used in initializer list: yes, intentionally
#endif

#include "llwin32headers.h"
#include <Psapi.h>               // S24
#include <VersionHelpers.h>
#include <vector>
#include <cassert>

#include "linden_common.h"

#include "llsys.h"

#include <iostream>

# include "zlib/zlib.h"

#include "llprocessor.h"
#include "llerrorcontrol.h"
#include "llevents.h"
#include "llformat.h"
#include "llregex.h"
#include "lltimer.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include <boost/bind/bind.hpp>

 // Boost.Bind placeholders for C++20 compatibility
using namespace boost::placeholders;
#include <boost/circular_buffer.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range.hpp>
#include "llfasttimer.h"

using namespace llsd;


LLCPUInfo gSysCPU;
LLMemoryInfo gSysMemory;

// Don't log memory info any more often than this. It also serves as our
// framerate sample size.
static const F32 MEM_INFO_THROTTLE = 20;
// Sliding window of samples. We intentionally limit the length of time we
// remember "the slowest" framerate because framerate is very slow at login.
// If we only triggered FrameWatcher logging when the session framerate
// dropped below the login framerate, we'd have very little additional data.
static const F32 MEM_INFO_WINDOW = 10*60;

LLOSInfo::LLOSInfo() :
    mMajorVer(0), mMinorVer(0), mBuild(0), mOSVersionString("")
{
	// S24 This first section remains as it pulls bitness quite reliably
    ///get native system info if available..
	//function pointer for loading GetNativeSystemInfo
    typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO); ///function pointer for loading GetNativeSystemInfo
    SYSTEM_INFO si; //System Info object file contains architecture info
    PGNSI pGNSI; //pointer object
    ZeroMemory(&si, sizeof(SYSTEM_INFO)); //zero out the memory in information
    pGNSI = (PGNSI)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetNativeSystemInfo"); //load kernel32 get function
    if (nullptr != pGNSI) //check if it has failed
    {
		pGNSI(&si); //success
    }
    else
    {
		GetSystemInfo(&si); //if it fails get regular system info 
    }
	

	// S24 - Read build number from registry (more reliable than file version)
	HKEY hKey;
	DWORD dwBuild = 0;
	DWORD dwType = REG_DWORD;
	DWORD dwSize = sizeof(DWORD);
	
	// Try to read CurrentBuildNumber from registry
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
					  L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 
					  0, 
					  KEY_QUERY_VALUE, 
					  &hKey) == ERROR_SUCCESS)
	{
		// First try CurrentBuild (DWORD) - available on newer Windows versions
		if (RegQueryValueExW(hKey, L"CurrentBuild", nullptr, &dwType, 
							 reinterpret_cast<LPBYTE>(&dwBuild), &dwSize) == ERROR_SUCCESS)
		{
			mBuild = dwBuild;
		}
		else
		{
			// Fallback to CurrentBuildNumber (string) for older versions
			wchar_t szBuild[32] = {0};
			dwSize = sizeof(szBuild);
			dwType = REG_SZ;
			
			if (RegQueryValueExW(hKey, L"CurrentBuildNumber", nullptr, &dwType,
								 reinterpret_cast<LPBYTE>(szBuild), &dwSize) == ERROR_SUCCESS)
			{
				mBuild = _wtoi(szBuild);
			}
		}
		
		RegCloseKey(hKey);
	}
	
	// Fallback to file version method if registry read failed
	if (mBuild == 0)
	{
		const auto system = L"kernel32.dll";
		DWORD dummy;
		const auto cbInfo =
			::GetFileVersionInfoSizeExW(FILE_VER_GET_NEUTRAL, system, &dummy);
		if (cbInfo > 0)
		{
			std::vector<char> buffer(cbInfo);
			::GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL, system, dummy,
				buffer.size(), &buffer[0]);
			void *p = nullptr;
			UINT size = 0;
			::VerQueryValueW(buffer.data(), L"\\", &p, &size);
			if (p != nullptr && size >= sizeof(VS_FIXEDFILEINFO))
			{
				auto pFixed = static_cast<const VS_FIXEDFILEINFO *>(p);
				mBuild = (HIWORD(pFixed->dwFileVersionLS));
			}
		}
	}

	if (mBuild >= 26200) // Windows 11 25H2 and later
	{
		mOSStringSimple = "Microsoft Windows 11 25H2+ ";
	}
	else if (mBuild >= 26100) // Windows 11 24H2
	{
		mOSStringSimple = "Microsoft Windows 11 24H2 ";
	}
	else if (mBuild >= 22631) // Windows 11 23H2
	{
		mOSStringSimple = "Microsoft Windows 11 23H2 ";
	}
	else if (mBuild >= 22621) // Windows 11 22H2
	{
		mOSStringSimple = "Microsoft Windows 11 22H2 ";
	}
	else if (mBuild >= 22000) // Windows 11 21H2 (initial release)
	{
		mOSStringSimple = "Microsoft Windows 11 21H2 ";
	}
	// Windows 10 build numbers
	else if (mBuild >= 19045) // Windows 10 22H2
	{
		mOSStringSimple = "Microsoft Windows 10 22H2 ";
	}
	else if (mBuild >= 19044) // Windows 10 21H2
	{
		mOSStringSimple = "Microsoft Windows 10 21H2 ";
	}
	else if (mBuild >= 19043) // Windows 10 21H1
	{
		mOSStringSimple = "Microsoft Windows 10 21H1 ";
	}
	else if (mBuild >= 19042) // Windows 10 20H2
	{
		mOSStringSimple = "Microsoft Windows 10 20H2 ";
	}
	else if (mBuild >= 19041) // Windows 10 2004
	{
		mOSStringSimple = "Microsoft Windows 10 2004 ";
	}
	else if (mBuild >= 18363) // Windows 10 1909
	{
		mOSStringSimple = "Microsoft Windows 10 1909 ";
	}
	else if (mBuild >= 18362) // Windows 10 1903
	{
		mOSStringSimple = "Microsoft Windows 10 1903 ";
	}
	else if (mBuild >= 17763) // Windows 10 1809
	{
		mOSStringSimple = "Microsoft Windows 10 1809 ";
	}
	else if (mBuild >= 17134) // Windows 10 1803
	{
		mOSStringSimple = "Microsoft Windows 10 1803 ";
	}
	else if (mBuild >= 16299) // Windows 10 1709
	{
		mOSStringSimple = "Microsoft Windows 10 1709 ";
	}
	else if (mBuild >= 15063) // Windows 10 1703
	{
		mOSStringSimple = "Microsoft Windows 10 1703 ";
	}
	else if (mBuild >= 14393) // Windows 10 1607
	{
		mOSStringSimple = "Microsoft Windows 10 1607 ";
	}
	else if (mBuild >= 10586) // Windows 10 1511
	{
		mOSStringSimple = "Microsoft Windows 10 1511 ";
	}
	else if (mBuild >= 10240) // Windows 10 1507 (initial release)
	{
		mOSStringSimple = "Microsoft Windows 10 1507 ";
	}
	else
	{
		// Anything older than Windows 10
		mOSStringSimple = "Microsoft Windows ";
	}

    //msdn microsoft finds 32 bit and 64 bit flavors this way..
    //http://msdn.microsoft.com/en-us/library/ms724429(VS.85).aspx (example code that contains quite a few more flavors
    //of windows than this code does (in case it is needed for the future)
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) //check for 64 bit
    {
        mOSStringSimple += "64-bit ";
    }
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
    {
        mOSStringSimple += "32-bit ";
    }

    mOSString = mOSStringSimple;
    if (mBuild > 0)
    {
		mOSString += llformat("(Build %d)", mBuild);
        mOSString += ")";
    }

    LLStringUtil::trim(mOSStringSimple);
    LLStringUtil::trim(mOSString);


    std::stringstream dotted_version_string;
    dotted_version_string << mMajorVer << "." << mMinorVer << "." << mBuild;
    mOSVersionString.append(dotted_version_string.str());
    // S24 all builds are 64bit............
    mOSBitness = is64Bit() ? 64 : 32;
    LL_INFOS("LLOSInfo") << "OS bitness: " << mOSBitness << LL_ENDL;

}

void LLOSInfo::stream(std::ostream& s) const
{
    s << mOSString;
}

const std::string& LLOSInfo::getOSString() const
{
    return mOSString;
}

const std::string& LLOSInfo::getOSStringSimple() const
{
    return mOSStringSimple;
}

const std::string& LLOSInfo::getOSVersionString() const
{
    return mOSVersionString;
}

const S32 LLOSInfo::getOSBitness() const
{
    return mOSBitness;
}

//static
U32 LLOSInfo::getProcessVirtualSizeKB()
{
    U32 virtual_size = 0;
    return virtual_size;
}

//static
U32 LLOSInfo::getProcessResidentSizeKB()
{
    U32 resident_size = 0;
    return resident_size;
}

//static
bool LLOSInfo::is64Bit()
{
    // S24: All builds are exclusively 64-bit
#if defined(_WIN64)
    return true;
#else
    // Should never reach here in production builds
    llassert(false && "32-bit build detected in 64-bit only codebase");
    return false;
#endif
}

LLCPUInfo::LLCPUInfo()
{
    std::ostringstream out;
    LLProcessorInfo proc;
    // proc.WriteInfoTextFile("procInfo.txt");
    mHasSSE = proc.hasSSE();
    mHasSSE2 = proc.hasSSE2();
    mHasSSE3 = proc.hasSSE3();
    mHasSSE3S = proc.hasSSE3S();
    mHasSSE41 = proc.hasSSE41();
    mHasSSE42 = proc.hasSSE42();
    mHasSSE4a = proc.hasSSE4a();
    mHasAltivec = proc.hasAltivec();
    mCPUMHz = (F64)proc.getCPUFrequency();
    mFamily = proc.getCPUFamilyName();
    mCPUString = "Unknown";

    out << proc.getCPUBrandName();
    if (200 < mCPUMHz && mCPUMHz < 10000)           // *NOTE: cpu speed is often way wrong, do a sanity check
    {
        out << " (" << mCPUMHz << " MHz)";
    }
    mCPUString = out.str();
    LLStringUtil::trim(mCPUString);

    if (mHasSSE)
    {
        mSSEVersions.append("1");
    }
    if (mHasSSE2)
    {
        mSSEVersions.append("2");
    }
    if (mHasSSE3)
    {
        mSSEVersions.append("3");
    }
    if (mHasSSE3S)
    {
        mSSEVersions.append("3S");
    }
    if (mHasSSE41)
    {
        mSSEVersions.append("4.1");
    }
    if (mHasSSE42)
    {
        mSSEVersions.append("4.2");
    }
    if (mHasSSE4a)
    {
        mSSEVersions.append("4a");
    }
}

bool LLCPUInfo::hasAltivec() const
{
    return mHasAltivec;
}

bool LLCPUInfo::hasSSE() const
{
    return mHasSSE;
}

bool LLCPUInfo::hasSSE2() const
{
    return mHasSSE2;
}

bool LLCPUInfo::hasSSE3() const
{
    return mHasSSE3;
}

bool LLCPUInfo::hasSSE3S() const
{
    return mHasSSE3S;
}

bool LLCPUInfo::hasSSE41() const
{
    return mHasSSE41;
}

bool LLCPUInfo::hasSSE42() const
{
    return mHasSSE42;
}

bool LLCPUInfo::hasSSE4a() const
{
    return mHasSSE4a;
}

F64 LLCPUInfo::getMHz() const
{
    return mCPUMHz;
}

std::string LLCPUInfo::getCPUString() const
{
    return mCPUString;
}

const LLSD& LLCPUInfo::getSSEVersions() const
{
    return mSSEVersions;
}

void LLCPUInfo::stream(std::ostream& s) const
{
    // gather machine information.
    s << LLProcessorInfo().getCPUFeatureDescription();

    // These are interesting as they reflect our internal view of the
    // CPU's attributes regardless of platform
    s << "->mHasSSE:     " << (U32)mHasSSE << std::endl;
    s << "->mHasSSE2:    " << (U32)mHasSSE2 << std::endl;
    s << "->mHasSSE3:    " << (U32)mHasSSE3 << std::endl;
    s << "->mHasSSE3S:    " << (U32)mHasSSE3S << std::endl;
    s << "->mHasSSE41:    " << (U32)mHasSSE41 << std::endl;
    s << "->mHasSSE42:    " << (U32)mHasSSE42 << std::endl;
    s << "->mHasSSE4a:    " << (U32)mHasSSE4a << std::endl;
    s << "->mHasAltivec: " << (U32)mHasAltivec << std::endl;
    s << "->mCPUMHz:     " << mCPUMHz << std::endl;
    s << "->mCPUString:  " << mCPUString << std::endl;
}

// Helper class for LLMemoryInfo: accumulate stats in the form we store for
// LLMemoryInfo::getStatsMap().
class Stats
{
public:
    Stats():
        mStats(LLSD::emptyMap())
    {}

    // Store every integer type as LLSD::Integer.
    template <class T>
    void add(const LLSD::String& name, const T& value,
             typename std::enable_if_t<std::is_integral_v<T> >* = 0)
    {
        mStats[name] = LLSD::Integer(value);
    }

    // Store every floating-point type as LLSD::Real.
    template <class T>
    void add(const LLSD::String& name, const T& value,
             typename std::enable_if_t<std::is_floating_point_v<T> >* = 0)
    {
        mStats[name] = LLSD::Real(value);
    }

    // Hope that LLSD::Date values are sufficiently unambiguous.
    void add(const LLSD::String& name, const LLSD::Date& value)
    {
        mStats[name] = value;
    }

    LLSD get() const { return mStats; }

private:
    LLSD mStats;
};

LLMemoryInfo::LLMemoryInfo()
{
    refresh();
}


static U32Kilobytes LLMemoryAdjustKBResult(U32Kilobytes inKB)
{
    // Moved this here from llfloaterabout.cpp

    //! \bug
    // For some reason, the reported amount of memory is always wrong.
    // The original adjustment assumes it's always off by one meg, however
    // errors of as much as 2520 KB have been observed in the value
    // returned from the GetMemoryStatusEx function.  Here we keep the
    // original adjustment from llfoaterabout.cpp until this can be
    // fixed somehow.
    inKB += U32Megabytes(1);

    return inKB;
}



U32Kilobytes LLMemoryInfo::getPhysicalMemoryKB() const
{
    return LLMemoryAdjustKBResult(U32Kilobytes(mStatsMap["Total Physical KB"].asInteger()));

}

//static
void LLMemoryInfo::getAvailableMemoryKB(U32Kilobytes& avail_mem_kb)
{
    // Sigh, this shouldn't be a static method, then we wouldn't have to
    // reload this data separately from refresh()
    LLSD statsMap(loadStatsMap());

    avail_mem_kb = (U32Kilobytes)statsMap["Avail Physical KB"].asInteger();

}

void LLMemoryInfo::stream(std::ostream& s) const
{
    // We want these memory stats to be easy to grep from the log, along with
    // the timestamp. So preface each line with the timestamp and a
    // distinctive marker. Without that, we'd have to search the log for the
    // introducer line, then read subsequent lines, etc...
    std::string pfx(LLError::utcTime() + " <mem> ");

    // Max key length
    size_t key_width(0);
    for (const auto& [key, value] : inMap(mStatsMap))
    {
        size_t len(key.length());
        if (len > key_width)
        {
            key_width = len;
        }
    }

    // Now stream stats
    for (const auto& [key, value] : inMap(mStatsMap))
    {
        s << pfx << std::setw(narrow<size_t>(key_width+1)) << (key + ':') << ' ';
        if (value.isInteger())
            s << std::setw(12) << value.asInteger();
        else if (value.isReal())
            s << std::fixed << std::setprecision(1) << value.asReal();
        else if (value.isDate())
            value.asDate().toStream(s);
        else
            s << value;           // just use default LLSD formatting
        s << std::endl;
    }
}

LLSD LLMemoryInfo::getStatsMap() const
{
    return mStatsMap;
}

LLMemoryInfo& LLMemoryInfo::refresh()
{
    LL_PROFILE_ZONE_SCOPED;
    mStatsMap = loadStatsMap();

    LL_DEBUGS("LLMemoryInfo") << "Populated mStatsMap:\n";
    LLSDSerialize::toPrettyXML(mStatsMap, LL_CONT);
    LL_ENDL;

    return *this;
}

LLSD LLMemoryInfo::loadStatsMap()
{
    LL_PROFILE_ZONE_SCOPED;

    // This implementation is derived from stream() code (as of 2011-06-29).
    Stats stats;

    // associate timestamp for analysis over time
    stats.add("timestamp", LLDate::now());

    MEMORYSTATUSEX state;
    state.dwLength = sizeof(state);
    GlobalMemoryStatusEx(&state);

    DWORDLONG div = 1024;

    stats.add("Percent Memory use", state.dwMemoryLoad/div);
    stats.add("Total Physical KB",  state.ullTotalPhys/div);
    stats.add("Avail Physical KB",  state.ullAvailPhys/div);
    stats.add("Total page KB",      state.ullTotalPageFile/div);
    stats.add("Avail page KB",      state.ullAvailPageFile/div);
    stats.add("Total Virtual KB",   state.ullTotalVirtual/div);
    stats.add("Avail Virtual KB",   state.ullAvailVirtual/div);

    // SL-12122 - Call to GetPerformanceInfo() was removed here. Took
    // on order of 10 ms, causing unacceptable frame time spike every
    // second, and results were never used. If this is needed in the
    // future, must find a way to avoid frame time impact (e.g. move
    // to another thread, call much less often).

    PROCESS_MEMORY_COUNTERS_EX pmem;
    pmem.cb = sizeof(pmem);
    // GetProcessMemoryInfo() is documented to accept either
    // PROCESS_MEMORY_COUNTERS* or PROCESS_MEMORY_COUNTERS_EX*, presumably
    // using the redundant size info to distinguish. But its prototype
    // specifically accepts PROCESS_MEMORY_COUNTERS*, and since this is a
    // classic-C API, PROCESS_MEMORY_COUNTERS_EX isn't a subclass. Cast the
    // pointer.
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*) &pmem, sizeof(pmem));

    stats.add("Page Fault Count",              pmem.PageFaultCount);
    stats.add("PeakWorkingSetSize KB",         pmem.PeakWorkingSetSize/div);
    stats.add("WorkingSetSize KB",             pmem.WorkingSetSize/div);
    stats.add("QutaPeakPagedPoolUsage KB",     pmem.QuotaPeakPagedPoolUsage/div);
    stats.add("QuotaPagedPoolUsage KB",        pmem.QuotaPagedPoolUsage/div);
    stats.add("QuotaPeakNonPagedPoolUsage KB", pmem.QuotaPeakNonPagedPoolUsage/div);
    stats.add("QuotaNonPagedPoolUsage KB",     pmem.QuotaNonPagedPoolUsage/div);
    stats.add("PagefileUsage KB",              pmem.PagefileUsage/div);
    stats.add("PeakPagefileUsage KB",          pmem.PeakPagefileUsage/div);
    stats.add("PrivateUsage KB",               pmem.PrivateUsage/div);

    return stats.get();
}

std::ostream& operator<<(std::ostream& s, const LLOSInfo& info)
{
    info.stream(s);
    return s;
}

std::ostream& operator<<(std::ostream& s, const LLCPUInfo& info)
{
    info.stream(s);
    return s;
}

std::ostream& operator<<(std::ostream& s, const LLMemoryInfo& info)
{
    info.stream(s);
    return s;
}

class FrameWatcher
{
public:
    // S24
    FrameWatcher():
        // Hooking onto the "mainloop" event pump gets us one call per frame.
        mConnection(LLEventPumps::instance()
            .obtain("mainloop")
            .listen("FrameWatcher", [this](const LLSD& event_data) {
                return tick(event_data); // S24-FRAMEWATCHER-BOOST-COMPAT: lambda return fixed to match boost::function<bool>
                })),
        mSampleStart(-1),
        // Initializing mSampleEnd to 0 ensures that we treat the first call
        // as the completion of a sample window.
        mSampleEnd(0),
        mFrames(0),
        // Both MEM_INFO_WINDOW and MEM_INFO_THROTTLE are in seconds. We need
        // the number of integer MEM_INFO_THROTTLE sample slots that will fit
        // in MEM_INFO_WINDOW. Round up.
        mSamples(static_cast<int>((MEM_INFO_WINDOW / MEM_INFO_THROTTLE) + 0.7)),
        // Initializing to F32_MAX means that the first real frame will become
        // the slowest ever, which sounds like a good idea.
        mSlowest(F32_MAX)
    {}

    bool tick(const LLSD&)
    {
        F32 timestamp(mTimer.getElapsedTimeF32());

        // Count this frame in the interval just completed.
        ++mFrames;

        // Have we finished a sample window yet?
        if (timestamp < mSampleEnd)
        {
            // no, just keep waiting
            return false;
        }

        // Set up for next sample window. Capture values for previous frame in
        // local variables and reset data members.
        U32 frames(mFrames);
        F32 sampleStart(mSampleStart);
        // No frames yet in next window
        mFrames = 0;
        // which starts right now
        mSampleStart = timestamp;
        // and ends MEM_INFO_THROTTLE seconds in the future
        mSampleEnd = mSampleStart + MEM_INFO_THROTTLE;

        // On the very first call, that's all we can do, no framerate
        // computation is possible.
        if (sampleStart < 0)
        {
            return false;
        }

        // How long did this actually take? As framerate slows, the duration
        // of the frame we just finished could push us WELL beyond our desired
        // sample window size.
        F32 elapsed(timestamp - sampleStart);
        F32 framerate(frames/elapsed);

        // Remember previous slowest framerate because we're just about to
        // update it.
        F32 slowest(mSlowest);
        // Remember previous number of samples.
        boost::circular_buffer<F32>::size_type prevSize(mSamples.size());

        // Capture new framerate in our samples buffer. Once the buffer is
        // full (after MEM_INFO_WINDOW seconds), this will displace the oldest
        // sample. ("So they all rolled over, and one fell out...")
        mSamples.push_back(framerate);

        // Calculate the new minimum framerate. I know of no way to update a
        // rolling minimum without ever rescanning the buffer. But since there
        // are only a few tens of items in this buffer, rescanning it is
        // probably cheaper (and certainly easier to reason about) than
        // attempting to optimize away some of the scans.
        mSlowest = framerate;       // pick an arbitrary entry to start
        for (boost::circular_buffer<F32>::const_iterator si(mSamples.begin()), send(mSamples.end());
             si != send; ++si)
        {
            if (*si < mSlowest)
            {
                mSlowest = *si;
            }
        }

        // We're especially interested in memory as framerate drops. Only log
        // when framerate drops below the slowest framerate we remember.
        // (Should always be true for the end of the very first sample
        // window.)
        if (framerate >= slowest)
        {
            return false;
        }
        // Congratulations, we've hit a new low.  :-P

        LL_INFOS("FrameWatcher") << ' ';
        if (! prevSize)
        {
            LL_CONT << "initial framerate ";
        }
        else
        {
            LL_CONT << "slowest framerate for last " << int(prevSize * MEM_INFO_THROTTLE)
                    << " seconds ";
        }

    auto precision = LL_CONT.precision();

        LL_CONT << std::fixed << std::setprecision(1) << framerate << '\n'
                << LLMemoryInfo();

    LL_CONT.precision(precision);
    LL_CONT << LL_ENDL;
        return false;
    }

private:
    // Storing the connection in an LLTempBoundListener ensures it will be
    // disconnected when we're destroyed.
    LLTempBoundListener mConnection;
    // Track elapsed time
    LLTimer mTimer;
    // Some of what you see here is in fact redundant with functionality you
    // can get from LLTimer. Unfortunately the LLTimer API is missing the
    // feature we need: has at least the stated interval elapsed, and if so,
    // exactly how long has passed? So we have to do it by hand, sigh.
    // Time at start, end of sample window
    F32 mSampleStart, mSampleEnd;
    // Frames this sample window
    U32 mFrames;
    // Sliding window of framerate samples
    boost::circular_buffer<F32> mSamples;
    // Slowest framerate in mSamples
    F32 mSlowest;
};

// Need an instance of FrameWatcher before it does any good
static FrameWatcher sFrameWatcher;

bool gunzip_file(const std::string& srcfile, const std::string& dstfile)
{
    std::string tmpfile;
    const S32 UNCOMPRESS_BUFFER_SIZE = 32768;
    bool retval = false;
    gzFile src = NULL;
    U8 buffer[UNCOMPRESS_BUFFER_SIZE];
    LLFILE *dst = NULL;
    S32 bytes = 0;
    tmpfile = dstfile + ".t";

    std::wstring utf16filename = ll_convert<std::wstring>(srcfile);
    src = gzopen_w(utf16filename.c_str(), "rb");

    if (! src) goto err;
    dst = LLFile::fopen(tmpfile, "wb");     /* Flawfinder: ignore */
    if (! dst) goto err;
    do
    {
        bytes = gzread(src, buffer, UNCOMPRESS_BUFFER_SIZE);
        size_t nwrit = fwrite(buffer, sizeof(U8), bytes, dst);
        if (nwrit < (size_t) bytes)
        {
            LL_WARNS() << "Short write on " << tmpfile << ": Wrote " << nwrit << " of " << bytes << " bytes." << LL_ENDL;
            goto err;
        }
    } while(gzeof(src) == 0);
    fclose(dst);
    dst = NULL;
    if (LLFile::rename(tmpfile, dstfile) == -1) goto err;       /* Flawfinder: ignore */
    retval = true;
err:
    if (src != NULL) gzclose(src);
    if (dst != NULL) fclose(dst);
    return retval;
}

bool gzip_file(const std::string& srcfile, const std::string& dstfile)
{
    const S32 COMPRESS_BUFFER_SIZE = 32768;
    std::string tmpfile;
    bool retval = false;
    U8 buffer[COMPRESS_BUFFER_SIZE];
    gzFile dst = NULL;
    LLFILE *src = NULL;
    S32 bytes = 0;
    tmpfile = dstfile + ".t";


    std::wstring utf16filename = ll_convert<std::wstring>(tmpfile);
    dst = gzopen_w(utf16filename.c_str(), "wb");


    if (! dst) goto err;
    src = LLFile::fopen(srcfile, "rb");     /* Flawfinder: ignore */
    if (! src) goto err;

    while ((bytes = (S32)fread(buffer, sizeof(U8), COMPRESS_BUFFER_SIZE, src)) > 0)
    {
        if (gzwrite(dst, buffer, bytes) <= 0)
        {
            LL_WARNS() << "gzwrite failed: " << gzerror(dst, NULL) << LL_ENDL;
            goto err;
        }
    }

    if (ferror(src))
    {
        LL_WARNS() << "Error reading " << srcfile << LL_ENDL;
        goto err;
    }

    gzclose(dst);
    dst = NULL;
    if (LLFile::rename(tmpfile, dstfile) == -1) goto err;       /* Flawfinder: ignore */
    retval = true;
 err:
    if (src != NULL) fclose(src);
    if (dst != NULL) gzclose(dst);
    return retval;
}
