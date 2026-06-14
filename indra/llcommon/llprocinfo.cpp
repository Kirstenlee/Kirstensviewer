/**
* @file llprocinfo.cpp
* @brief Process, cpu and resource usage information APIs.
* @author monty@lindenlab.com
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2013, Linden Research, Inc.
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

#include "llprocinfo.h"

#define	PSAPI_VERSION	1
#include "windows.h"
#include "psapi.h"

// S24 modern version of static
void LLProcInfo::getCPUUsage(time_type& user_time, time_type& system_time)
{
	// Get a handle to the current process (do not close this handle).
	HANDLE hProcess = GetCurrentProcess();

	// Retrieve process times.
	FILETIME ftCreation, ftExit, ftKernel, ftUser;
	if (!GetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel, &ftUser))
	{
		// Handle error as appropriate: initializing times to 0.
		user_time = 0;
		system_time = 0;
		return;
	}

	// Helper: Convert FILETIME (which is in 100 ns units) to microseconds.
	auto fileTimeToMicroseconds = [](const FILETIME& ft) -> unsigned long long {
		ULARGE_INTEGER uli;
		uli.LowPart = ft.dwLowDateTime;
		uli.HighPart = ft.dwHighDateTime;
		return uli.QuadPart / 10ULL;  // 100 ns / 10 = 10 ns → 1 µs scaling factor.
		};

	system_time = fileTimeToMicroseconds(ftKernel);
	user_time = fileTimeToMicroseconds(ftUser);
}