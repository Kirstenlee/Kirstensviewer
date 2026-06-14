/**
 * @file llprocessor.cpp
 * @brief Code to figure out the processor. Originally by Benjamin Jurke.
 *  Updated 2025 By KirstenLee Cinquetti.
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
#include "llprocessor.h"
#include "llstring.h"
#include "stringize.h"
#include "llerror.h"

#include <iomanip>
 //#include <memory>

#if LL_WINDOWS
#	include "llwin32headers.h"
#	define _interlockedbittestandset _renamed_interlockedbittestandset
#	define _interlockedbittestandreset _renamed_interlockedbittestandreset
#	include <intrin.h>
#	undef _interlockedbittestandset
#	undef _interlockedbittestandreset
#endif

#include "llsd.h"

class LLProcessorInfoImpl; // foward declaration for the mImpl;
// S24 ENHANCED
namespace CPUInfo {
	// -------------------------------------------------------------------------
	// CPU Information Enumeration and Names
	// -------------------------------------------------------------------------
	enum cpu_info {
		eBrandName = 0,
		eFrequency,
		eVendor,
		eStepping,
		eFamily,
		eExtendedFamily,
		eModel,
		eExtendedModel,
		eType,
		eBrandID,
		eFamilyName,
		// Modern additions:
		eCoreCount,
		eThreadCount
	};

	constexpr const char* cpu_info_names[] = {
		"Processor Name",
		"Frequency",
		"Vendor",
		"Stepping",
		"Family",
		"Extended Family",
		"Model",
		"Extended Model",
		"Type",
		"Brand ID",
		"Family Name",
		"Core Count",
		"Thread Count"
	};

	// -------------------------------------------------------------------------
	// CPU Configuration Enumeration and Names
	// -------------------------------------------------------------------------
	enum cpu_config {
		eMaxID,
		eMaxExtID,
		eCLFLUSHCacheLineSize,
		eAPICPhysicalID,
		eCacheLineSize,
		eL2Associativity,
		eCacheSizeK,
		eFeatureBits,
		eExtFeatureBits,
		// Modern cache info (if available):
		eL1CacheSize,
		eL2CacheSize,
		eL3CacheSize
	};

	constexpr const char* cpu_config_names[] = {
		"Max Supported CPUID level",
		"Max Supported Ext. CPUID level",
		"CLFLUSH cache line size",
		"APIC Physical ID",
		"Cache Line Size",
		"L2 Associativity",
		"Cache Size",
		"Feature Bits",
		"Ext. Feature Bits",
		"L1 Cache Size",
		"L2 Cache Size",
		"L3 Cache Size"
	};

	// -------------------------------------------------------------------------
	// CPU Features Enumeration and Names
	// -------------------------------------------------------------------------
	// Note: The enum values here were originally chosen to line up with specific
	// CPUID bitfields. When adding new features, ensure that the numbers do not conflict.
	// If necessary you can extend them beyond the first 32 bits.
	enum cpu_features {
		eSSE_Ext = 25,
		eSSE2_Ext = 26,
		eSSE3_Features = 32,
		eMONITOR_MWAIT = 33,
		eCPLDebugStore = 34,
		eThermalMonitor2 = 35,
		eAltivec = 36,
		eSSE3S_Features = 37,
		eSSE4_1_Features = 38,
		eSSE4_2_Features = 39,
		eSSE4a_Features = 40,
		// Modern features
		eAVX_Features = 41,
		eAVX2_Features = 42,
		eAVX512F_Features = 43
	};

	constexpr const char* cpu_feature_names[] = {
		"x87 FPU On Chip",                 // 0
		"Virtual-8086 Mode Enhancement",   // 1
		"Debugging Extensions",            // 2
		"Page Size Extensions",            // 3
		"Time Stamp Counter",              // 4
		"RDMSR and WRMSR Support",         // 5
		"Physical Address Extensions",     // 6
		"Machine Check Exception",         // 7
		"CMPXCHG8B Instruction",           // 8
		"APIC On Chip",                    // 9
		"Unknown1",                        // 10
		"SYSENTER and SYSEXIT",            // 11
		"Memory Type Range Registers",     // 12
		"PTE Global Bit",                  // 13
		"Machine Check Architecture",      // 14
		"Conditional Move/Compare Instruction", // 15
		"Page Attribute Table",            // 16
		"Page Size Extension",             // 17
		"Processor Serial Number",         // 18
		"CFLUSH Extension",                // 19
		"Unknown2",                        // 20
		"Debug Store",                     // 21
		"Thermal Monitor and Clock Ctrl",  // 22
		"MMX Technology",                  // 23
		"FXSAVE/FXRSTOR",                  // 24
		"SSE Extensions",                  // 25  == eSSE_Ext
		"SSE2 Extensions",                 // 26  == eSSE2_Ext
		"Self Snoop",                      // 27
		"Hyper-threading Technology",      // 28
		"Thermal Monitor",                 // 29
		"Unknown4",                        // 30
		"Pend. Brk. EN.",                  // 31 End of FeatureInfo bits
		"SSE3 New Instructions",           // 32
		"MONITOR/MWAIT",                   // 33
		"CPL Qualified Debug Store",       // 34
		"Thermal Monitor 2",               // 35
		"Altivec",                         // 36
		"SSE3S Instructions",              // 37
		"SSE4.1 Instructions",             // 38
		"SSE4.2 Instructions",             // 39
		"SSE4a Instructions",              // 40
		"AVX Instructions",                // 41
		"AVX2 Instructions",               // 42
		"AVX-512F Instructions"            // 43
	};

	// -------------------------------------------------------------------------
	// Intel/AMD CPU Family Naming Functions
	// -------------------------------------------------------------------------
	inline std::string intel_CPUFamilyName(int composed_family) {
		switch (composed_family) {
		case 3:   return "Intel i386";
		case 4:   return "Intel i486";
		case 5:   return "Intel Pentium";
		case 6:   return "Intel Pentium Pro/2/3, Core";
		case 7:   return "Intel Itanium (IA-64)";
		case 0xF: return "Intel Pentium 4";
		case 0x10:return "Intel Itanium 2 (IA-64)";
		default:  return "Intel <unknown 0x" + std::to_string(static_cast<unsigned int>(composed_family)) + ">";
		}
	}

	inline std::string amd_CPUFamilyName(int composed_family) {
		switch (composed_family) {
		case 4:   return "AMD 80486/5x86";
		case 5:   return "AMD K5/K6";
		case 6:   return "AMD K7";
		case 0xF: return "AMD K8";
		case 0x10:return "AMD K8L";
		case 0x12:return "AMD K10";
		case 0x14:return "AMD Bobcat";
		case 0x15:return "AMD Bulldozer";
		case 0x16:return "AMD Jaguar";
		case 0x17:return "AMD Zen/Zen+/Zen2";
		case 0x18:return "AMD Hygon Dhyana";
		case 0x19:return "AMD Zen 3";
		default:  return "AMD <unknown 0x" + std::to_string(static_cast<unsigned int>(composed_family)) + ">";
		}
	}

	inline std::string compute_CPUFamilyName(const char* cpu_vendor, int family, int ext_family) {
		constexpr std::string_view intel_vendor = "GenuineIntel";
		constexpr std::string_view amd_vendor = "AuthenticAMD";
		std::string vendor(cpu_vendor);

		if (vendor.rfind(std::string(intel_vendor), 0) == 0) {
			unsigned int composed_family = static_cast<unsigned int>(family + ext_family);
			return intel_CPUFamilyName(composed_family);
		}
		else if (vendor.rfind(std::string(amd_vendor), 0) == 0) {
			unsigned int composed_family = (family == 0xF)
				? static_cast<unsigned int>(family + ext_family)
				: static_cast<unsigned int>(family);
			return amd_CPUFamilyName(composed_family);
		}
		return "Unrecognized CPU vendor <" + vendor + ">";
	}
} // end namespace CPUInfo

// The base class for implementations.
// Each platform should override this class.
class LLProcessorInfoImpl
{
public:
	LLProcessorInfoImpl()
	{
		mProcessorInfo["info"] = LLSD::emptyMap();
		mProcessorInfo["config"] = LLSD::emptyMap();
		mProcessorInfo["extension"] = LLSD::emptyMap();
	}
	virtual ~LLProcessorInfoImpl() {}

	F64 getCPUFrequency() const
	{
		return getInfo(CPUInfo::eFrequency, 0).asReal();
	}

	bool hasSSE() const
	{
		return hasExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE_Ext]);
	}

	bool hasSSE2() const
	{
		return hasExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE2_Ext]);
	}

	bool hasSSE3() const
	{
		return hasExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE3_Features]);
	}

	bool hasSSE3S() const
	{
		return hasExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE3S_Features]);
	}

	bool hasSSE41() const
	{
		return hasExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE4_1_Features]);
	}

	bool hasSSE42() const
	{
		return hasExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE4_2_Features]);
	}

	bool hasSSE4a() const
	{
		return hasExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE4a_Features]);
	}

	bool hasAltivec() const
	{
		return hasExtension("Altivec");
	}

	std::string getCPUFamilyName() const { return getInfo(CPUInfo::eFamilyName, "Unset family").asString(); }
	std::string getCPUBrandName() const { return getInfo(CPUInfo::eBrandName, "Unset brand").asString(); }

	// *NOTE:Mani - I didn't want to screw up server use of this data...
	virtual std::string getCPUFeatureDescription() const
	{
		std::ostringstream out;
		out << std::endl << std::endl;
		out << "// CPU General Information" << std::endl;
		out << "//////////////////////////" << std::endl;
		out << "Processor Name:   " << getCPUBrandName() << std::endl;
		out << "Frequency:        " << getCPUFrequency() << " MHz" << std::endl;
		out << "Vendor:           " << getInfo(CPUInfo::eVendor, "Unset vendor").asString() << std::endl;
		out << "Family:           " << getCPUFamilyName() << " (" << getInfo(CPUInfo::eFamily, 0) << ")" << std::endl;
		out << "Extended family:  " << getInfo(CPUInfo::eExtendedFamily, 0) << std::endl;
		out << "Model:            " << getInfo(CPUInfo::eModel, 0) << std::endl;
		out << "Extended model:   " << getInfo(CPUInfo::eExtendedModel, 0) << std::endl;
		out << "Type:             " << getInfo(CPUInfo::eType, 0) << std::endl;
		out << "Brand ID:         " << getInfo(CPUInfo::eBrandID, 0) << std::endl;
		out << std::endl;
		out << "// CPU Configuration" << std::endl;
		out << "//////////////////////////" << std::endl;

		// Iterate through the dictionary of configuration options.
		LLSD configs = mProcessorInfo["config"];
		for (LLSD::map_const_iterator cfgItr = configs.beginMap(); cfgItr != configs.endMap(); ++cfgItr)
		{
			out << cfgItr->first << " = " << cfgItr->second << std::endl;
		}
		out << std::endl;

		out << "// CPU Extensions" << std::endl;
		out << "//////////////////////////" << std::endl;

		for (LLSD::map_const_iterator itr = mProcessorInfo["extension"].beginMap(); itr != mProcessorInfo["extension"].endMap(); ++itr)
		{
			out << "  " << itr->first << std::endl;
		}
		return out.str();
	}

protected:
	void setInfo(CPUInfo::cpu_info info_type, const LLSD& value)
	{
		setInfo(CPUInfo::cpu_info_names[info_type], value);
	}
	LLSD getInfo(CPUInfo::cpu_info info_type, const LLSD& defaultVal) const
	{
		return getInfo(CPUInfo::cpu_info_names[info_type], defaultVal);
	}

	void setConfig(CPUInfo::cpu_config config_type, const LLSD& value)
	{
		setConfig(CPUInfo::cpu_config_names[config_type], value);
	}
	LLSD getConfig(CPUInfo::cpu_config config_type, const LLSD& defaultVal) const
	{
		return getConfig(CPUInfo::cpu_config_names[config_type], defaultVal);
	}

	void setExtension(const std::string& name) { mProcessorInfo["extension"][name] = "true"; }
	bool hasExtension(const std::string& name) const
	{
		return mProcessorInfo["extension"].has(name);
	}

private:
	void setInfo(const std::string& name, const LLSD& value) { mProcessorInfo["info"][name] = value; }
	LLSD getInfo(const std::string& name, const LLSD& defaultVal) const
	{
		if (mProcessorInfo["info"].has(name))
		{
			return mProcessorInfo["info"][name];
		}
		return defaultVal;
	}
	void setConfig(const std::string& name, const LLSD& value) { mProcessorInfo["config"][name] = value; }
	LLSD getConfig(const std::string& name, const LLSD& defaultVal) const
	{
		LLSD r = mProcessorInfo["config"].get(name);
		return r.isDefined() ? r : defaultVal;
	}

private:

	LLSD mProcessorInfo;
};

// some of the following code
// uses the MSVC compiler intrinsics __cpuid() and __rdtsc().

// Delays for the specified amount of milliseconds
static void _Delay(unsigned int ms)
{
	LARGE_INTEGER freq, c1, c2;
	__int64 x;

	// Get High-Res Timer frequency
	if (!QueryPerformanceFrequency(&freq))
		return;

	// Convert ms to High-Res Timer value
	x = freq.QuadPart / 1000 * ms;

	// Get first snapshot of High-Res Timer value
	QueryPerformanceCounter(&c1);
	do
	{
		// Get second snapshot
		QueryPerformanceCounter(&c2);
	} while (c2.QuadPart - c1.QuadPart < x);
	// Loop while (second-first < x)
}

static F64 calculate_cpu_frequency(U32 measure_msecs)
{
	if (measure_msecs == 0)
	{
		return 0;
	}

	// After that we declare some vars and check the frequency of the high
	// resolution timer for the measure process.
	// If there"s no high-res timer, we exit.
	unsigned __int64 starttime, endtime, timedif, freq, start, end, dif;
	if (!QueryPerformanceFrequency((LARGE_INTEGER*)&freq))
	{
		return 0;
	}

	// Now we can init the measure process. We set the process and thread priority
	// to the highest available level (Realtime priority). Also we focus the
	// first processor in the multiprocessor system.
	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();
	unsigned long dwCurPriorityClass = GetPriorityClass(hProcess);
	int iCurThreadPriority = GetThreadPriority(hThread);
	DWORD_PTR dwProcessMask, dwSystemMask, dwNewMask = 1;
	GetProcessAffinityMask(hProcess, &dwProcessMask, &dwSystemMask);

	SetPriorityClass(hProcess, REALTIME_PRIORITY_CLASS);
	SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL);
	SetProcessAffinityMask(hProcess, dwNewMask);

	//// Now we call a CPUID to ensure, that all other prior called functions are
	//// completed now (serialization)
	//__asm cpuid
	int cpu_info[4] = { -1 };
	__cpuid(cpu_info, 0);

	// We ask the high-res timer for the start time
	QueryPerformanceCounter((LARGE_INTEGER*)&starttime);

	// Then we get the current cpu clock and store it
	start = __rdtsc();

	// Now we wart for some msecs
	_Delay(measure_msecs);
	//	Sleep(uiMeasureMSecs);

	// We ask for the end time
	QueryPerformanceCounter((LARGE_INTEGER*)&endtime);

	// And also for the end cpu clock
	end = __rdtsc();

	// Now we can restore the default process and thread priorities
	SetProcessAffinityMask(hProcess, dwProcessMask);
	SetThreadPriority(hThread, iCurThreadPriority);
	SetPriorityClass(hProcess, dwCurPriorityClass);

	// Then we calculate the time and clock differences
	dif = end - start;
	timedif = endtime - starttime;

	// And finally the frequency is the clock difference divided by the time
	// difference.
	F64 frequency = (F64)dif / (((F64)timedif) / freq);

	// At last we just return the frequency that is also stored in the call
	// member var uqwFrequency - converted to MHz
	return frequency / (F64)1000000;
}

// Windows implementation
class LLProcessorInfoWindowsImpl : public LLProcessorInfoImpl
{
public:
	LLProcessorInfoWindowsImpl()
	{
		getCPUIDInfo();
		setInfo(CPUInfo::eFrequency, calculate_cpu_frequency(50));
	}

private:
	void getCPUIDInfo()
	{
		// http://msdn.microsoft.com/en-us/library/hskdteyh(VS.80).aspx

		// __cpuid with an InfoType argument of 0 returns the number of
		// valid Ids in cpu_info[0] and the CPU identification string in
		// the other three array elements. The CPU identification string is
		// not in linear order. The code below arranges the information
		// in a human readable form.
		int cpu_info[4] = { -1 };
		__cpuid(cpu_info, 0);
		unsigned int ids = (unsigned int)cpu_info[0];
		setConfig(CPUInfo::eMaxID, (S32)ids);

		char cpu_vendor[0x20];
		memset(cpu_vendor, 0, sizeof(cpu_vendor));
		*((int*)cpu_vendor) = cpu_info[1];
		*((int*)(cpu_vendor + 4)) = cpu_info[3];
		*((int*)(cpu_vendor + 8)) = cpu_info[2];
		setInfo(CPUInfo::eVendor, cpu_vendor);
		std::string cmp_vendor(cpu_vendor);
		bool is_amd = false;
		if (cmp_vendor == "AuthenticAMD")
		{
			is_amd = true;
		}

		// Get the information associated with each valid Id
		for (unsigned int i = 0; i <= ids; ++i)
		{
			__cpuid(cpu_info, i);

			// Interpret CPU feature information.
			if (i == 1)
			{
				setInfo(CPUInfo::eStepping, cpu_info[0] & 0xf);
				setInfo(CPUInfo::eModel, (cpu_info[0] >> 4) & 0xf);
				int family = (cpu_info[0] >> 8) & 0xf;
				setInfo(CPUInfo::eFamily, family);
				setInfo(CPUInfo::eType, (cpu_info[0] >> 12) & 0x3);
				setInfo(CPUInfo::eExtendedModel, (cpu_info[0] >> 16) & 0xf);
				int ext_family = (cpu_info[0] >> 20) & 0xff;
				setInfo(CPUInfo::eExtendedFamily, ext_family);
				setInfo(CPUInfo::eBrandID, cpu_info[1] & 0xff);

				setInfo(CPUInfo::eFamilyName, CPUInfo::compute_CPUFamilyName(cpu_vendor, family, ext_family));

				setConfig(CPUInfo::eCLFLUSHCacheLineSize, ((cpu_info[1] >> 8) & 0xff) * 8);
				setConfig(CPUInfo::eAPICPhysicalID, (cpu_info[1] >> 24) & 0xff);

				if (cpu_info[2] & 0x1)
				{
					setExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE3_Features]);
				}

				if (cpu_info[2] & 0x8)
				{
					// S24 correcting a long standing typo!
					setExtension(CPUInfo::cpu_feature_names[CPUInfo::eMONITOR_MWAIT]);
				}

				if (cpu_info[2] & 0x10)
				{
					setExtension(CPUInfo::cpu_feature_names[CPUInfo::eCPLDebugStore]);
				}

				if (cpu_info[2] & 0x100)
				{
					setExtension(CPUInfo::cpu_feature_names[CPUInfo::eThermalMonitor2]);
				}

				if (cpu_info[2] & 0x200)
				{
					setExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE3S_Features]);
				}

				if (cpu_info[2] & 0x80000)
				{
					setExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE4_1_Features]);
				}

				if (cpu_info[2] & 0x100000)
				{
					setExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE4_2_Features]);
				}

				unsigned int feature_info = (unsigned int)cpu_info[3];
				for (unsigned int index = 0, bit = 1; index < CPUInfo::eSSE3_Features; ++index, bit <<= 1)
				{
					if (feature_info & bit)
					{
						setExtension(CPUInfo::cpu_feature_names[index]);
					}
				}
			}
		}

		// Calling __cpuid with 0x80000000 as the InfoType argument
		// gets the number of valid extended IDs.
		__cpuid(cpu_info, 0x80000000);
		unsigned int ext_ids = cpu_info[0];
		setConfig(CPUInfo::eMaxExtID, 0);

		char cpu_brand_string[0x40];
		memset(cpu_brand_string, 0, sizeof(cpu_brand_string));

		// Get the information associated with each extended ID.
		for (unsigned int i = 0x80000000; i <= ext_ids; ++i)
		{
			__cpuid(cpu_info, i);

			// Interpret CPU brand string and cache information.
			if (i == 0x80000001)
			{
				if (is_amd)
				{
					setExtension(CPUInfo::cpu_feature_names[CPUInfo::eSSE4a_Features]);
				}
			}
			else if (i == 0x80000002)
			{
				memcpy(cpu_brand_string, cpu_info, sizeof(cpu_info));
			}
			else if (i == 0x80000003)
				memcpy(cpu_brand_string + 16, cpu_info, sizeof(cpu_info));
			else if (i == 0x80000004)
			{
				memcpy(cpu_brand_string + 32, cpu_info, sizeof(cpu_info));
				setInfo(CPUInfo::eBrandName, cpu_brand_string);
			}
			else if (i == 0x80000006)
			{
				setConfig(CPUInfo::eCacheLineSize, cpu_info[2] & 0xff);
				setConfig(CPUInfo::eL2Associativity, (cpu_info[2] >> 12) & 0xf);
				setConfig(CPUInfo::eCacheSizeK, (cpu_info[2] >> 16) & 0xffff);
			}
		}
	}
};

//////////////////////////////////////////////////////
// Interface definition
// S24 : Helper function that returns the unique instance.
// S24 : In C++11, such a local static is initialized in a thread-safe manner.
static LLProcessorInfoImpl* getProcessorInfoImpl()
{
	static LLProcessorInfoWindowsImpl s_impl;
	return &s_impl;
}

LLProcessorInfo::LLProcessorInfo()
	: mImpl(getProcessorInfoImpl())
{
}

LLProcessorInfo::~LLProcessorInfo() {}
F64MegahertzImplicit LLProcessorInfo::getCPUFrequency() const { return mImpl->getCPUFrequency(); }
bool LLProcessorInfo::hasSSE() const { return mImpl->hasSSE(); }
bool LLProcessorInfo::hasSSE2() const { return mImpl->hasSSE2(); }
bool LLProcessorInfo::hasSSE3() const { return mImpl->hasSSE3(); }
bool LLProcessorInfo::hasSSE3S() const { return mImpl->hasSSE3S(); }
bool LLProcessorInfo::hasSSE41() const { return mImpl->hasSSE41(); }
bool LLProcessorInfo::hasSSE42() const { return mImpl->hasSSE42(); }
bool LLProcessorInfo::hasSSE4a() const { return mImpl->hasSSE4a(); }
bool LLProcessorInfo::hasAltivec() const { return mImpl->hasAltivec(); }
std::string LLProcessorInfo::getCPUFamilyName() const { return mImpl->getCPUFamilyName(); }
std::string LLProcessorInfo::getCPUBrandName() const { return mImpl->getCPUBrandName(); }
std::string LLProcessorInfo::getCPUFeatureDescription() const { return mImpl->getCPUFeatureDescription(); }