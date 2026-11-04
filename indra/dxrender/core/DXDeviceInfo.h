/**
 * @file DXDeviceInfo.h
 * @brief GPU/driver capability info, split out of dxrender's root llgl.h
 * into core/ to sit next to DXDevice (it's DXGI-adapter/driver info, not
 * pipeline state) and renamed from LLGLManager/gGLManager - this is purely
 * DXGI-sourced data under DX_RENDER, nothing GL about it. See
 * project_dxrender_consolidation design notes.
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

#ifndef DX_DXDEVICEINFO_H
#define DX_DXDEVICEINFO_H

#include <string>
#include "stdtypes.h"

class LLSD;

// Manage GL extensions...
class DXDeviceInfo
{
public:
    DXDeviceInfo();

    bool initGL();
    void shutdownGL();

    // S24: initGL() above is pure OpenGL and never called under DX_RENDER.
    // initGLDX() is the real DXGI-based equivalent that populates
    // mGLVendor/mGLRenderer/mGLVersion/etc - without it those stay at
    // zero/empty defaults, which starves LLFeatureManager of real GPU data.
    bool initGLDX();

    std::string getRawGLString(); // For sending to simulator

    bool mInited;
    bool mIsDisabled;

    // OpenGL limits
    S32 mMaxSamples;
    S32 mNumTextureImageUnits;
    S32 mMaxSampleMaskWords;
    S32 mMaxColorTextureSamples;
    S32 mMaxDepthTextureSamples;
    S32 mMaxIntegerSamples;
    S32 mGLMaxVertexRange;
    S32 mGLMaxIndexRange;
    S32 mGLMaxTextureSize;
    // initGL() (llgl.cpp), which normally queries and sanity-clamps this to
    // 65536, never runs (no real GL context) - defaulting to 0 here breaks
    // every consumer that divides by it to size a permutation macro (e.g.
    // llviewershadermgr.cpp's make_gltf_variant() computing MAX_UBO_VEC4S =
    // 0, producing an illegal zero-size HLSL array, X3059). 65536 matches
    // the real GL path's own clamp value, which is also D3D11's guaranteed
    // minimum constant-buffer size.
    S32 mMaxUniformBlockSize = 65536;
    S32 mMaxVaryingVectors = 0;

    // GL 4.x capabilities
    bool mHasCubeMapArray = false;
    bool mHasDebugOutput = false;
    bool mHasTransformFeedback = false;

    bool mIsAMD;
    bool mIsNVIDIA;
    bool mIsIntel;
    bool mIsApple = false;

    // hints to the render pipe
    U32 mDownScaleMethod = 0; // see settings.xml RenderDownScaleMethod

#if LL_DARWIN
    // Needed to distinguish problem cards on older Macs that break with Materials
    bool mIsMobileGF;
#endif

    // Whether this version of GL is good enough for SL to use
    bool mHasRequirements;

    S32 mDriverVersionMajor;
    S32 mDriverVersionMinor;
    S32 mDriverVersionRelease;
    F32 mGLVersion; // e.g = 1.4
    S32 mGLSLVersionMajor;
    S32 mGLSLVersionMinor;
    std::string mDriverVersionVendorString;
    std::string mGLVersionString;

    U32 mVRAM; // VRAM in MB

    // Live, OS-reported figures sourced from IDXGIAdapter3::QueryVideoMemoryInfo,
    // refreshed on RegisterVideoMemoryBudgetChangeNotificationEvent (see
    // LLWindowWin32Thread in llwindowwin32.cpp). 0 means unavailable (non-Windows,
    // or DXGI adapter/query failed) -- callers must fall back to mVRAM/self-estimates.
    U32 mVRAMBudget = 0;       // DXGI_QUERY_VIDEO_MEMORY_INFO::Budget, in MB
    U32 mVRAMCurrentUsage = 0; // DXGI_QUERY_VIDEO_MEMORY_INFO::CurrentUsage, in MB

    std::string getGLInfoString();
    void printGLInfoString();
    void getGLInfo(LLSD& info);

    void asLLSD(LLSD& info);

    // In ALL CAPS
    std::string mGLVendor;
    std::string mGLVendorShort;

    // In ALL CAPS
    std::string mGLRenderer;

private:
    void initGLStates();
};

extern DXDeviceInfo gDXDeviceInfo;

#endif // DX_DXDEVICEINFO_H
