/**
 * @file DXDeviceInfo.cpp
 * @brief DXDeviceInfo (renamed from LLGLManager) implementation, split out
 * of dxrender's root llgl.cpp into core/ alongside DXDevice.
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

#include "linden_common.h"

#include "DXDeviceInfo.h"
#include "DXDevice.h"
#include "DXState.h"

#include "llsd.h"
#include "llstring.h"
#include "lldxhardware.h"

#include <dxgi.h>

static const std::string HEADLESS_VENDOR_STRING("Kirstens Viewer");
static const std::string HEADLESS_RENDERER_STRING("Headless");
static const std::string HEADLESS_VERSION_STRING("1.0");

extern bool gHeadlessClient;

DXDeviceInfo gDXDeviceInfo;
// S24 - Re-order to match initialisation
DXDeviceInfo::DXDeviceInfo() :
    mInited(false),
    mIsDisabled(false),
    mMaxSamples(0),
    mNumTextureImageUnits(1),
    mMaxSampleMaskWords(0),
    mMaxColorTextureSamples(0),
    mMaxDepthTextureSamples(0),
    mMaxIntegerSamples(0),
    mGLMaxVertexRange(0),
    mGLMaxIndexRange(0),
    mIsAMD(false),
    mIsNVIDIA(false),
    mIsIntel(false),
    mHasRequirements(true),
    mDriverVersionMajor(1),
    mDriverVersionMinor(0),
    mDriverVersionRelease(0),
    mGLVersion(1.0f),
    mGLSLVersionMajor(0),
    mGLSLVersionMinor(0),
    mVRAM(0)

{
}

// S24: legacy WGL/GL context-init path; unreachable under DX_RENDER
// (LLWindowWin32::switchContext() only calls this from its non-DX_RENDER
// branch). Real equivalent is initGLDX() below. Body removed; name/signature
// kept in case anything still calls this.
bool DXDeviceInfo::initGL()
{
    if (mInited)
    {
        LL_ERRS("RenderInit") << "Calling init on DXDeviceInfo after already initialized!" << LL_ENDL;
    }

    return true;
}

// S24: real DXGI-based equivalent of initGL() above - see llgl.h. Called
// once from LLWindowWin32::switchContext() after initDX11Context() succeeds.
bool DXDeviceInfo::initGLDX()
{
    if (mInited)
    {
        LL_ERRS("RenderInit") << "Calling init on DXDeviceInfo after already initialized!" << LL_ENDL;
    }

    mInited = true;
    mHasRequirements = true;

    // Walk up from the already-created device to its owning adapter (same
    // technique DXSwapChain::create() already uses to find the swap chain's
    // factory) rather than re-enumerating adapters and guessing which one
    // matches - this guarantees we describe the actual adapter in use.
    ID3D11Device* device = gDXDevice.getDevice();
    if (device)
    {
        IDXGIDevice* dxgi_device = nullptr;
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device)) && dxgi_device)
        {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && adapter)
            {
                DXGI_ADAPTER_DESC desc = {};
                if (SUCCEEDED(adapter->GetDesc(&desc)))
                {
                    std::wstring description_w(desc.Description);
                    mGLRenderer = ll_convert_wide_to_string(description_w);
                    LLStringUtil::toUpper(mGLRenderer);

                    mVRAM = (U32)(desc.DedicatedVideoMemory / (1024 * 1024));

                    switch (desc.VendorId)
                    {
                    case 0x10DE: // NVIDIA
                        mGLVendorShort = "NVIDIA";
                        mIsNVIDIA = true;
                        break;
                    case 0x1002: // AMD/ATI
                    case 0x1022:
                        mGLVendorShort = "AMD";
                        mIsAMD = true;
                        break;
                    case 0x8086: // Intel
                        mGLVendorShort = "INTEL";
                        mIsIntel = true;
                        break;
                    default:
                        mGLVendorShort = "MISC";
                        break;
                    }

                    // S24: DXGI has no separate "vendor name" string like
                    // GL_VENDOR - use the PCI-VendorId-derived generic name
                    // from the switch above instead of duplicating the model
                    // string (Description) into both fields.
                    mGLVendor = mGLVendorShort;
                }
                adapter->Release();
            }
            dxgi_device->Release();
        }
    }

    // S24: D3D11 feature-level hardware is capability-equivalent to (or
    // beyond) every GL-version-gated feature-masking check in this
    // codebase; report a real modern version rather than leaving mGLVersion
    // at its 1.0f default, which trips LLFeatureManager::applyBaseMasks()'s
    // "mGLVersion < 3.99f" check into unconditionally applying the "GL3"
    // feature mask.
    mGLVersion = 4.6f;
    mDriverVersionMajor = 4;
    mDriverVersionMinor = 6;
    mDriverVersionRelease = 0;
    mGLSLVersionMajor = 4;
    mGLSLVersionMinor = 60;

    // Real feature-level string for display purposes (e.g. Floater About's
    // "Graphics API" line, llappviewer.cpp) - distinct from mGLVersion above,
    // which is a fixed feature-masking sentinel, not meant to be shown to
    // the user as-is.
    switch (gDXDevice.getFeatureLevel())
    {
    case D3D_FEATURE_LEVEL_11_1: mGLVersionString = "Direct3D 11.1 (Feature Level 11_1)"; break;
    case D3D_FEATURE_LEVEL_11_0: mGLVersionString = "Direct3D 11.0 (Feature Level 11_0)"; break;
    case D3D_FEATURE_LEVEL_10_1: mGLVersionString = "Direct3D 11 (Feature Level 10_1)"; break;
    case D3D_FEATURE_LEVEL_10_0: mGLVersionString = "Direct3D 11 (Feature Level 10_0)"; break;
    default: mGLVersionString = "Direct3D 11"; break;
    }

    mHasCubeMapArray = true;
    mHasTransformFeedback = true;
    mHasDebugOutput = true;

    mGLMaxTextureSize = 16384;
    mNumTextureImageUnits = 32;

    LL_INFOS("RenderInit") << "DX_RENDER GPU detection: vendor=" << mGLVendorShort
        << " renderer=" << mGLRenderer << " vram=" << mVRAM << "MB" << LL_ENDL;

    return true;
}

void DXDeviceInfo::getGLInfo(LLSD& info)
{
    if (gHeadlessClient)
    {
        info["GLInfo"]["GLVendor"] = HEADLESS_VENDOR_STRING;
        info["GLInfo"]["GLRenderer"] = HEADLESS_RENDERER_STRING;
        info["GLInfo"]["GLVersion"] = HEADLESS_VERSION_STRING;
        return;
    }
    else
    {
        // S24: use the already-populated fields, not raw glGetString() -
        // there is no GL context to call it against under DX_RENDER.
        info["GLInfo"]["GLVendor"] = mGLVendor;
        info["GLInfo"]["GLRenderer"] = mGLRenderer;
        info["GLInfo"]["GLVersion"] = mGLVersionString;
    }
}

std::string DXDeviceInfo::getGLInfoString()
{
    std::string info_str;

    if (gHeadlessClient)
    {
        info_str += std::string("GL_VENDOR      ") + HEADLESS_VENDOR_STRING + std::string("\n");
        info_str += std::string("GL_RENDERER    ") + HEADLESS_RENDERER_STRING + std::string("\n");
        info_str += std::string("GL_VERSION     ") + HEADLESS_VERSION_STRING + std::string("\n");
    }
    else
    {
        // S24: same as getGLInfo() above.
        info_str += std::string("GL_VENDOR      ") + mGLVendor + std::string("\n");
        info_str += std::string("GL_RENDERER    ") + mGLRenderer + std::string("\n");
        info_str += std::string("GL_VERSION     ") + mGLVersionString + std::string("\n");
    }

    return info_str;
}

void DXDeviceInfo::printGLInfoString()
{
    if (gHeadlessClient)
    {
        LL_INFOS("RenderInit") << "GL_VENDOR:     " << HEADLESS_VENDOR_STRING << LL_ENDL;
        LL_INFOS("RenderInit") << "GL_RENDERER:   " << HEADLESS_RENDERER_STRING << LL_ENDL;
        LL_INFOS("RenderInit") << "GL_VERSION:    " << HEADLESS_VERSION_STRING << LL_ENDL;
    }
    else
    {
        // S24: same as getGLInfo() above.
        LL_INFOS("RenderInit") << "GL_VENDOR:     " << mGLVendor << LL_ENDL;
        LL_INFOS("RenderInit") << "GL_RENDERER:   " << mGLRenderer << LL_ENDL;
        LL_INFOS("RenderInit") << "GL_VERSION:    " << mGLVersionString << LL_ENDL;
    }
}

std::string DXDeviceInfo::getRawGLString()
{
    std::string gl_string;
    if (gHeadlessClient)
    {
        gl_string = HEADLESS_VENDOR_STRING + " " + HEADLESS_RENDERER_STRING;
    }
    else
    {
        // S24: same as getGLInfo() above; also feeds
        // LLFeatureManager's RDNA3.5 detection (checkRDNA35()).
        gl_string = mGLVendor + " " + mGLRenderer;
    }
    return gl_string;
}

void DXDeviceInfo::asLLSD(LLSD& info)
{
    // Currently these are duplicates of fields in "system".
    info["gpu_vendor"] = mGLVendorShort;
    info["gpu_version"] = mDriverVersionVendorString;
    info["opengl_version"] = mGLVersionString;

    info["vram"] = LLSD::Integer(mVRAM);

    // OpenGL limits
    info["max_samples"] = mMaxSamples;
    info["num_texture_image_units"] =  mNumTextureImageUnits;
    info["max_sample_mask_words"] = mMaxSampleMaskWords;
    info["max_color_texture_samples"] = mMaxColorTextureSamples;
    info["max_depth_texture_samples"] = mMaxDepthTextureSamples;
    info["max_integer_samples"] = mMaxIntegerSamples;
    info["max_vertex_range"] = mGLMaxVertexRange;
    info["max_index_range"] = mGLMaxIndexRange;
    info["max_texture_size"] = mGLMaxTextureSize;

    // Which vendor
    info["is_ati"] = mIsAMD;  // note, do not rename is_ati to is_amd without coordinating with DW
    info["is_nvidia"] = mIsNVIDIA;
    info["is_intel"] = mIsIntel;

    info["gl_renderer"] = mGLRenderer;
}

// S24: queryAvailableVRAM() removed - zero callers anywhere in the tree (confirmed by grep), and
// its two glGetIntegerv() calls (NVX/ATI GL memory-info extensions) were unguarded raw GL, dead
// code with a live-landmine body. Found in a tree-wide stray-GL sweep.

void DXDeviceInfo::shutdownGL()
{
    if (mInited)
    {
        // S24: no GL glFinish() under DX_RENDER - the DX11 device/swapchain are already fully
        // torn down by the caller before this runs (llwindowwin32.cpp), so there's no
        // outstanding GPU work left to wait on.
        mInited = false;
    }
}

// S24: initExtensions() removed - zero callers anywhere in the tree (confirmed by grep),
// a legacy full-GL function-pointer-table loader (~875 lines of glXxx = GLH_EXT_GET_PROC_ADDRESS(...)
// assignments) with no live D3D11 relevance. Found in a tree-wide stray-GL sweep.

void DXDeviceInfo::initGLStates()
{
    //gl states moved to classes in llglstates.h
    DXState::initClass();
}
