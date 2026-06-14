/**
 * @file lldxhardware.cpp
 * @brief LLDXHardware implementation
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifdef LL_WINDOWS

 // S24 Version

#include "linden_common.h"

// Include Windows headers FIRST to set up WinSock2 properly
#include "llwin32headers.h"

#define INITGUID
#include <dxdiag.h>
#undef INITGUID

#include <wbemidl.h>
#include <comdef.h>

#include <boost/tokenizer.hpp>

#include "lldxhardware.h"

#include "llerror.h"

#include "llstring.h"
#include "llstl.h"
#include "lltimer.h"

#include <algorithm>   // for std::max
#pragma comment(lib, "wbemuuid.lib")

LLDXHardware gDXHardware;

//-----------------------------------------------------------------------------
// Defines and Constants
//-----------------------------------------------------------------------------
#define SAFE_DELETE(p)       { if(p) { delete (p);     (p) = nullptr; } }
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p) = nullptr; } }
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p) = nullptr; } }

typedef BOOL(WINAPI* PfnCoSetProxyBlanket)(IUnknown* pProxy, DWORD dwAuthnSvc, DWORD dwAuthzSvc,
	OLECHAR* pServerPrincName, DWORD dwAuthnLevel, DWORD dwImpLevel,
	RPC_AUTH_IDENTITY_HANDLE pAuthInfo, DWORD dwCapabilities);

//Getting the version of graphics controller driver via WMI
std::string LLDXHardware::getDriverVersionWMI(EGPUVendor vendor)
{
	std::string driverVersionResult;
	HRESULT hres;
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	IWbemLocator* pLoc = nullptr;

	hres = CoCreateInstance(
		CLSID_WbemLocator,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IWbemLocator, reinterpret_cast<void**>(&pLoc));

	if (FAILED(hres))
	{
		LL_DEBUGS("AppInit") << "Failed to initialize COM library. Error code = 0x" << std::hex << hres << LL_ENDL;
		return std::string();
	}

	IWbemServices* pSvc = nullptr;

	// Connect to the root\cimv2 namespace with
	// the current user and obtain pointer pSvc
	// to make IWbemServices calls.
	hres = pLoc->ConnectServer(
		_bstr_t(L"ROOT\\CIMV2"),  // WMI namespace
		nullptr,                 // User name (current)
		nullptr,                 // User password (current)
		0,                       // Locale
		0L,                 // Security flags
		0,                       // Authority
		0,                       // Context
		&pSvc                    // pointer to IWbemServices proxy
	);

	if (FAILED(hres))
	{
		LL_WARNS("AppInit") << "Could not connect. Error code = 0x" << std::hex << hres << LL_ENDL;
		pLoc->Release();
		CoUninitialize();
		return std::string();
	}

	LL_DEBUGS("AppInit") << "Connected to ROOT\\CIMV2 WMI namespace" << LL_ENDL;

	// Set security levels on the proxy
	hres = CoSetProxyBlanket(
		pSvc,
		RPC_C_AUTHN_WINNT,
		RPC_C_AUTHZ_NONE,
		nullptr,
		RPC_C_AUTHN_LEVEL_CALL,
		RPC_C_IMP_LEVEL_IMPERSONATE,
		nullptr,
		EOAC_NONE                    // proxy capabilities
	);

	if (FAILED(hres))
	{
		LL_WARNS("AppInit") << "Could not set proxy blanket. Error code = 0x" << std::hex << hres << LL_ENDL;
		pSvc->Release();
		pLoc->Release();
		CoUninitialize();
		return std::string();
	}
	IEnumWbemClassObject* pEnumerator = nullptr;

	// Get the data from the query
	ULONG uReturn = 0;
	hres = pSvc->ExecQuery(
		bstr_t("WQL"),
		bstr_t("SELECT * FROM Win32_VideoController"), // You might filter using Availability for disabled controllers.
		WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
		nullptr,
		&pEnumerator);

	if (FAILED(hres))
	{
		LL_WARNS("AppInit") << "Query for operating system name failed. Error code = 0x" << std::hex << hres << LL_ENDL;
		pSvc->Release();
		pLoc->Release();
		CoUninitialize();
		return std::string();
	}

	while (pEnumerator)
	{
		IWbemClassObject* pclsObj = nullptr;
		hres = pEnumerator->Next(WBEM_INFINITE, 1,
			&pclsObj, &uReturn);

		if (0 == uReturn)
		{
			break;  // No more objects.
		}

		if (vendor != GPU_ANY)
		{
			VARIANT vtCaptionProp;
			// Might be preferable to check "AdapterCompatibility" here instead of caption.
			hres = pclsObj->Get(L"Caption", 0, &vtCaptionProp, nullptr, nullptr);

			if (FAILED(hres))
			{
				LL_WARNS("AppInit") << "Query for Caption property failed. Error code = 0x" << std::hex << hres << LL_ENDL;
				pSvc->Release();
				pLoc->Release();
				CoUninitialize();
				return std::string();
			}

			// use characters in the returned driver version
			BSTR caption(vtCaptionProp.bstrVal);

			//convert BSTR to std::string
			std::wstring wsCaption(vtCaptionProp.bstrVal, SysStringLen(vtCaptionProp.bstrVal));
			std::string captionStr = ll_convert_wide_to_string(wsCaption);
			LLStringUtil::toLower(captionStr);

			bool found = false;
			switch (vendor)
			{
			case GPU_INTEL:
				found = (captionStr.find("intel") != std::string::npos);
				break;
			case GPU_NVIDIA:
				found = (captionStr.find("nvidia") != std::string::npos);
				break;
			case GPU_AMD:
				found = (captionStr.find("amd") != std::string::npos ||
					captionStr.find("ati ") != std::string::npos ||
					captionStr.find("radeon") != std::string::npos);
				break;
			default:
				break;
			}

			if (found)
			{
				VariantClear(&vtCaptionProp);
			}
			else
			{
				VariantClear(&vtCaptionProp);
				pclsObj->Release();
				continue;
			}
		}

		VARIANT vtVersionProp;

		// Get the value of the DriverVersion property
		hres = pclsObj->Get(L"DriverVersion", 0, &vtVersionProp, nullptr, nullptr);

		if (FAILED(hres))
		{
			LL_WARNS("AppInit") << "Query for DriverVersion property failed. Error code = 0x" << std::hex << hres << LL_ENDL;
			pSvc->Release();
			pLoc->Release();
			CoUninitialize();
			return std::string();
		}

		// use characters in the returned driver version
		BSTR driverVersion(vtVersionProp.bstrVal);

		//convert BSTR to std::string
		std::wstring wsDriver(vtVersionProp.bstrVal, SysStringLen(vtVersionProp.bstrVal));
		std::string driverVerStr = ll_convert_wide_to_string(wsDriver);
		LL_INFOS("AppInit") << "DriverVersion: " << driverVerStr << LL_ENDL;

		if (driverVersionResult.empty())
		{
			driverVersionResult = driverVerStr;
		}
		else if (driverVersionResult != driverVerStr)
		{
			if (vendor == GPU_ANY)
			{
                // Expected from systems with gpus from different vendors
				LL_INFOS("DriverVersion") << "Multiple video drivers detected. Version of second driver: " << driverVerStr << LL_ENDL;
			}
			else
			{
				// Not Expected!
				LL_WARNS("DriverVersion") << "Multiple video drivers detected from same vendor. Version of second driver: " << driverVerStr << LL_ENDL;
			}
		}

		VariantClear(&vtVersionProp);
		pclsObj->Release();
	}

	// Cleanup
	// ========
	if (pSvc)
	{
		pSvc->Release();
	}
	if (pLoc)
	{
		pLoc->Release();
	}
	if (pEnumerator)
	{
		pEnumerator->Release();
	}

	// supposed to always call CoUninitialize even if init returned false
	CoUninitialize();

	return driverVersionResult;
}

void get_wstring(IDxDiagContainer* containerp, const WCHAR* wszPropName, WCHAR* wszPropValue, int outputSize)
{
	// Validate parameters.
	if (!containerp || !wszPropName || !wszPropValue || outputSize <= 0)
	{
		return;
	}

	HRESULT hr = S_OK;
	VARIANT var;

	VariantInit(&var);
	hr = containerp->GetProp(wszPropName, &var);
	if (SUCCEEDED(hr))
	{
		// Parse the variant based on its type.
		switch (var.vt)
		{
		case VT_UI4:
			swprintf_s(wszPropValue, outputSize, L"%u", var.ulVal);
			break;
		case VT_I4:
			swprintf_s(wszPropValue, outputSize, L"%d", var.lVal);
			break;
		case VT_BOOL:
			wcscpy_s(wszPropValue, outputSize, (var.boolVal ? L"true" : L"false"));
			break;
		case VT_BSTR:
			wcsncpy_s(wszPropValue, outputSize, var.bstrVal, _TRUNCATE);
			wszPropValue[0] = L'\0';
			break;
		}
	}
	// Clear the variant (this is needed to free BSTR memory)
	VariantClear(&var);
}

std::string get_string(IDxDiagContainer* containerp, const WCHAR* wszPropName)
{
	WCHAR wszPropValue[256];
	get_wstring(containerp, wszPropName, wszPropValue, 256);

	return ll_convert<std::string>(std::wstring(wszPropValue));
}

LLDXHardware::LLDXHardware()
{
}

void LLDXHardware::cleanup()
{
}

LLSD LLDXHardware::getDisplayInfo()
{
	LLTimer hw_timer;
	HRESULT       hr;
	LLSD ret;
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	IDxDiagProvider* dx_diag_providerp = nullptr;
	IDxDiagContainer* dx_diag_rootp = nullptr;
	IDxDiagContainer* devices_containerp = nullptr;
	IDxDiagContainer* device_containerp = nullptr;
	IDxDiagContainer* file_containerp = nullptr;
	IDxDiagContainer* driver_containerp = nullptr;
	DWORD dw_device_count = 0;

	// CoCreate a IDxDiagProvider*
	LL_INFOS() << "CoCreateInstance IID_IDxDiagProvider" << LL_ENDL;
	hr = CoCreateInstance(CLSID_DxDiagProvider,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IDxDiagProvider,
		reinterpret_cast<void**>(&dx_diag_providerp));

	if (FAILED(hr))
	{
		LL_WARNS() << "No DXDiag provider found!  DirectX 9 not installed!" << LL_ENDL;
		goto LCleanup;
	}
	if (SUCCEEDED(hr)) // if FAILED(hr) then dx9 is not installed
	{
		// Fill out a DXDIAG_INIT_PARAMS struct and pass it to IDxDiagContainer::Initialize
		// Passing in TRUE for bAllowWHQLChecks, allows dxdiag to check if drivers are
		// digital signed as logo'd by WHQL which may connect via internet to update
		// WHQL certificates.
		DXDIAG_INIT_PARAMS dx_diag_init_params;
		ZeroMemory(&dx_diag_init_params, sizeof(dx_diag_init_params));

		dx_diag_init_params.dwSize = sizeof(DXDIAG_INIT_PARAMS);
		dx_diag_init_params.dwDxDiagHeaderVersion = DXDIAG_DX9_SDK_VERSION;
		dx_diag_init_params.bAllowWHQLChecks = TRUE;
		dx_diag_init_params.pReserved = nullptr;

		LL_INFOS() << "dx_diag_providerp->Initialize" << LL_ENDL;
		hr = dx_diag_providerp->Initialize(&dx_diag_init_params);
		if (FAILED(hr))
		{
			goto LCleanup;
		}

		LL_INFOS() << "dx_diag_providerp->GetRootContainer" << LL_ENDL;
		hr = dx_diag_providerp->GetRootContainer(&dx_diag_rootp);
		if (FAILED(hr) || dx_diag_rootp == nullptr)
		{
			goto LCleanup;
		}

		HRESULT hr;

		// Get the display devices container.
		LL_INFOS() << "dx_diag_rootp->GetChildContainer(DxDiag_DisplayDevices)" << LL_ENDL;
		hr = dx_diag_rootp->GetChildContainer(L"DxDiag_DisplayDevices", &devices_containerp);
		if (FAILED(hr) || devices_containerp == nullptr)
		{
			// do not release 'dirty' devices_containerp at this stage, only dx_diag_rootp
			devices_containerp = nullptr;
			goto LCleanup;
		}

		// Make sure there is at least one device.
		hr = devices_containerp->GetNumberOfChildContainers(&dw_device_count);
		if (FAILED(hr) || dw_device_count == 0)
		{
			goto LCleanup;
		}

		// Get device "0" (assumed to be the primary device).
		LL_INFOS() << "devices_containerp->GetChildContainer(0)" << LL_ENDL;
		hr = devices_containerp->GetChildContainer(L"0", &device_containerp);
		if (FAILED(hr) || device_containerp == nullptr)
		{
			goto LCleanup;
		}

		// Retrieve the English VRAM string.
		std::string ram_str = get_string(device_containerp, L"szDisplayMemoryEnglish");


		// Dump the string as an int into the structure
		char* stopstring = nullptr;
		ret["VRAM"] = LLSD::Integer(strtol(ram_str.c_str(), &stopstring, 10));
		std::string device_name = get_string(device_containerp, L"szDescription");
		ret["DeviceName"] = device_name;
		std::string device_driver = get_string(device_containerp, L"szDriverVersion");
		ret["DriverVersion"] = device_driver;

		// ATI devices use a slightly different version string via a registry key.
		if (device_name.length() >= 4 && device_name.substr(0, 4) == "ATI ")
		{
			// get the key
			HKEY hKey = nullptr;
			const DWORD RV_SIZE = 100;
			WCHAR release_version[RV_SIZE] = { 0 };

			// Hard coded registry entry.  Using this since it's simpler for now.
			// And using EnumDisplayDevices to get a registry key also requires
			// a hard coded Query value.
			if (ERROR_SUCCESS == RegOpenKey(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\ATI Technologies\\CBT"), &hKey))
			{
				// get the value
				DWORD dwType = REG_SZ;
				DWORD dwSize = sizeof(WCHAR) * RV_SIZE;
				if (ERROR_SUCCESS == RegQueryValueEx(hKey, TEXT("ReleaseVersion"),
					nullptr, &dwType, reinterpret_cast<LPBYTE>(release_version), &dwSize))
				{
					// print the value
					// Windows might not null-terminate, so we ensure it.
					release_version[RV_SIZE - 1] = 0;
					ret["DriverVersion"] = utf16str_to_utf8str(release_version);
				}
				RegCloseKey(hKey);
			}
		}
	}

LCleanup:
	if (!ret.isMap() || (ret.size() == 0))
	{
		LL_INFOS() << "Failed to get data, cleaning up" << LL_ENDL;
	}
	SAFE_RELEASE(file_containerp);
	SAFE_RELEASE(driver_containerp);
	SAFE_RELEASE(device_containerp);
	SAFE_RELEASE(devices_containerp);
	SAFE_RELEASE(dx_diag_rootp);
	SAFE_RELEASE(dx_diag_providerp);

	CoUninitialize();
	return ret;
}

#endif