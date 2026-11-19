/** 
 * @file llmemory.cpp
 * @brief Very special memory allocation/deallocation stuff here
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

#include "linden_common.h"


#include "llthread.h"


#include <psapi.h>

#include "llmemory.h"

#include "llsys.h"
#include "llframetimer.h"
#include "lltrace.h"
#include "llerror.h"
//----------------------------------------------------------------------------

//static

// most important memory metric for texture streaming
//  On Windows, this should agree with resource monitor -> performance -> memory -> available
//
// NOTE: this number MAY be less than the actual available memory on systems with more than MaxHeapSize64 GB of physical memory (default 16GB)
//  In that case, should report min(available, sMaxHeapSizeInKB-sAllocateMemInKB)
U32Kilobytes LLMemory::sAvailPhysicalMemInKB(U32_MAX);

// Installed physical memory
U32Kilobytes LLMemory::sMaxPhysicalMemInKB(0);

// Maximimum heap size according to the user's settings (default 16GB)
U32Kilobytes LLMemory::sMaxHeapSizeInKB(U32_MAX);

// Current memory usage
U32Kilobytes LLMemory::sAllocatedMemInKB(0);

U32Kilobytes LLMemory::sAllocatedPageSizeInKB(0);


static LLTrace::SampleStatHandle<F64Megabytes> sAllocatedMem("allocated_mem", "active memory in use by application");
static LLTrace::SampleStatHandle<F64Megabytes> sVirtualMem("virtual_mem", "virtual memory assigned to application");

void ll_assert_aligned_func(uintptr_t ptr,U32 alignment)
{
	#ifdef SHOW_ASSERT
		// Redundant, place to set breakpoints.
		if (ptr%alignment!=0)
		{
			LL_WARNS() << "alignment check failed" << LL_ENDL;
		}
		llassert(ptr%alignment==0);
	#endif
}

//static 
void LLMemory::initMaxHeapSizeGB(F32Gigabytes max_heap_size)
{
	sMaxHeapSizeInKB = U32Kilobytes::convert(max_heap_size);
}

//static 
void LLMemory::updateMemoryInfo() 
{

    sMaxPhysicalMemInKB = gSysMemory.getPhysicalMemoryKB();

    U32Kilobytes avail_mem;
    LLMemoryInfo::getAvailableMemoryKB(avail_mem);
    sAvailPhysicalMemInKB = avail_mem;

	PROCESS_MEMORY_COUNTERS counters;

	if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
	{
		LL_WARNS() << "GetProcessMemoryInfo failed" << LL_ENDL;
		return ;
	}

	sAllocatedMemInKB = U32Kilobytes::convert(U64Bytes(counters.WorkingSetSize));
	sAllocatedPageSizeInKB = U32Kilobytes::convert(U64Bytes(counters.PagefileUsage));
	sample(sVirtualMem, sAllocatedPageSizeInKB);


	sample(sAllocatedMem, sAllocatedMemInKB);

	// S24 : unsigned subtraction wraps to a huge value once the working set exceeds the cap
	const U32Kilobytes headroom = (sAllocatedMemInKB < sMaxHeapSizeInKB)
		? U32Kilobytes(sMaxHeapSizeInKB - sAllocatedMemInKB) : U32Kilobytes(0);
	sAvailPhysicalMemInKB = llmin(sAvailPhysicalMemInKB, headroom);

	return ;
}

// S24
#include "llwin32headers.h"

// Probe if a block of virtual address space of 'size' bytes can be reserved
// at 'desired_addr' (or anywhere if nullptr). No commit is done.
// Returns the address where it would fit, or nullptr on failure.
void* LLMemory::tryToAlloc(void* desired_addr, U32 size)
{
	if (size == 0)
	{
		LL_WARNS() << "tryToAlloc called with size=0" << LL_ENDL;
		return nullptr;
	}

	// Reserve only, no commit
	void* reserved = ::VirtualAlloc(desired_addr,
		size,
		MEM_RESERVE | MEM_TOP_DOWN,
		PAGE_NOACCESS);

	if (!reserved)
	{
		DWORD err = ::GetLastError();
		LL_WARNS() << "VirtualAlloc probe failed. size=" << size
			<< " addr=" << desired_addr
			<< " err=" << err << LL_ENDL;
		return nullptr;
	}

	// Immediately release the reservation
	if (!::VirtualFree(reserved, 0, MEM_RELEASE))
	{
		DWORD err = ::GetLastError();
		LL_ERRS() << "VirtualFree failed after probe. addr=" << reserved
			<< " err=" << err << LL_ENDL;
		return nullptr;
	}

	return reserved; // valid address range, but no longer reserved
}

//static 
void LLMemory::logMemoryInfo(bool update)
{
	if(update)
	{
		updateMemoryInfo() ;
	}

    LL_INFOS() << llformat("Current allocated physical memory: %.2f MB", sAllocatedMemInKB / 1024.0) << LL_ENDL;
    LL_INFOS() << llformat("Current allocated page size: %.2f MB", sAllocatedPageSizeInKB / 1024.0) << LL_ENDL;
    LL_INFOS() << llformat("Current available physical memory: %.2f MB", sAvailPhysicalMemInKB / 1024.0) << LL_ENDL;
    LL_INFOS() << llformat("Current max usable memory: %.2f MB", sMaxPhysicalMemInKB / 1024.0) << LL_ENDL;
}

//static 
U32Kilobytes LLMemory::getAvailableMemKB() 
{
	return sAvailPhysicalMemInKB ;
}

//static 
U32Kilobytes LLMemory::getMaxMemKB() 
{
	return sMaxPhysicalMemInKB ;
}

//static 
U32Kilobytes LLMemory::getAllocatedMemKB() 
{
	return sAllocatedMemInKB ;
}

//----------------------------------------------------------------------------


//static 
U64 LLMemory::getCurrentRSS()
{
	PROCESS_MEMORY_COUNTERS counters;

	if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
	{
		LL_WARNS() << "GetProcessMemoryInfo failed" << LL_ENDL;
		return 0;
	}

	return counters.WorkingSetSize;
}

