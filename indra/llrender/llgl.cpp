/**
 * @file llgl.cpp
 * @brief LLGL implementation
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

// This file sets some global GL parameters, and implements some
// useful functions for GL operations.

#include "linden_common.h"

#include "boost/tokenizer.hpp"

#include "llsys.h"

#include "llgl.h"
#include "llglstates.h"
#include "llrender.h"

#include "llerror.h"
#include "llerrorcontrol.h"
#include "llquaternion.h"
#include "llmath.h"
#include "m4math.h"
#include "llstring.h"
#include "llstacktrace.h"

#include "llglheaders.h"
#include "llhlslshader.h"

#include "glm/glm.hpp"
#include <glm/gtc/matrix_access.hpp>
#include "glm/gtc/type_ptr.hpp"

#include "lldxhardware.h"

#include "DXStateCache.h"
#include "DXDevice.h"
#include "DXUIBatch.h"
#include <dxgi.h>



bool gDebugSession = false;
bool gClothRipple = false;
bool gHeadlessClient = false;
bool gNonInteractive = false;
bool gDXActive = false; // S24: renamed from gGLActive - see llgl.h

static const std::string HEADLESS_VENDOR_STRING("Kirstens Viewer");
static const std::string HEADLESS_RENDERER_STRING("Headless");
static const std::string HEADLESS_VERSION_STRING("1.0");

llofstream gFailLog;

// S24: gl_debug_callback() (glDebugMessageCallback-based GL debug logger)
// removed - GL-only; DX_RENDER's debug layer is DXDevice's sDebugLayerEnabled.

void parse_glsl_version(S32& major, S32& minor);

void ll_init_fail_log(std::string filename)
{
    gFailLog.open(filename.c_str());
}


void ll_fail(std::string msg)
{

    if (gDebugSession)
    {
        std::vector<std::string> lines;

        gFailLog << LLError::utcTime() << " " << msg << std::endl;

        gFailLog << "Stack Trace:" << std::endl;

        ll_get_stack_trace(lines);

        for(size_t i = 0; i < lines.size(); ++i)
        {
            gFailLog << lines[i] << std::endl;
        }

        gFailLog << "End of Stack Trace." << std::endl << std::endl;

        gFailLog.flush();
    }
};

void ll_close_fail_log()
{
    gFailLog.close();
}

LLMatrix4 gDXObliqueProjectionInverse;

#define LL_GL_NAME_POOLING 0

std::list<LLGLUpdate*> LLGLUpdate::sGLQ;

// S24: the glext/glh_extensions PFN*PROC pointer definitions that lived here were
// removed along with LLGLManager::initWGL() (their only assigner, dead under DX_RENDER)
// - see llglheaders.h.

LLGLManager gGLManager;
// S24 - Re-order to match initialisation
LLGLManager::LLGLManager() :
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
bool LLGLManager::initGL()
{
    if (mInited)
    {
        LL_ERRS("RenderInit") << "Calling init on LLGLManager after already initialized!" << LL_ENDL;
    }

    return true;
}

// S24: real DXGI-based equivalent of initGL() above - see llgl.h. Called
// once from LLWindowWin32::switchContext() after initDX11Context() succeeds.
bool LLGLManager::initGLDX()
{
    if (mInited)
    {
        LL_ERRS("RenderInit") << "Calling init on LLGLManager after already initialized!" << LL_ENDL;
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

void LLGLManager::getGLInfo(LLSD& info)
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

std::string LLGLManager::getGLInfoString()
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

void LLGLManager::printGLInfoString()
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

std::string LLGLManager::getRawGLString()
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

void LLGLManager::asLLSD(LLSD& info)
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

void LLGLManager::shutdownGL()
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

void rotate_quat(LLQuaternion& rotation)
{
    F32 angle_radians, x, y, z;
    rotation.getAngleAxis(&angle_radians, &x, &y, &z);
    gDX.rotatef(angle_radians * RAD_TO_DEG, x, y, z);
}

void flush_glerror()
{
    // S24: no-op under DX_RENDER - glGetError() with no current GL context is undefined
    // behavior, and DX_RENDER never creates one.
}

void clear_glerror()
{
    // S24: no-op under DX_RENDER, same reasoning as flush_glerror() above - glGetError() is
    // unresolved once OpenGL is delinked, so a debug+DX_RENDER build would otherwise fail to
    // link.
}

///////////////////////////////////////////////////////////////
//
// DXState
//

// Static members
std::unordered_map<LLGLenum, LLGLboolean> DXState::sStateMap;
LLGLenum DXState::sCullFace = GL_BACK; // OpenGL default

bool DXDepthTest::sDepthEnabled = false;
DXenum DXDepthTest::sDepthFunc = GL_LESS; // OpenGL default
bool DXDepthTest::sWriteEnabled = true;

//static
void DXState::initClass()
{
    sStateMap[GL_DITHER] = GL_TRUE;
    // sStateMap[GL_TEXTURE_2D] = GL_TRUE;

    //make sure multisample defaults to disabled
    // S24: GL_MULTISAMPLE isn't one of applyDXState()'s dispatched states (see that function's
    // own comment - only BLEND/CULL_FACE/SCISSOR_TEST/DEPTH_CLAMP/POLYGON_OFFSET_* have a D3D11
    // target), so this sStateMap bookkeeping is all that ever matters under DX_RENDER; MSAA
    // itself is a swap-chain/render-target sample-count property in D3D11, not a per-draw
    // enable/disable.
    sStateMap[GL_MULTISAMPLE] = GL_FALSE;
}

//static
void DXState::restoreGL()
{
    sStateMap.clear();
    initClass();
}

// S24: resetTextureStates()/dumpStates() had zero callers anywhere in the tree (confirmed by
// grep, not assumed) - removed rather than left as unreferenced raw-GL-calling dead code
// (resetTextureStates() had 3 unguarded calls: glGetIntegerv/glClientActiveTexture). Unlike
// checkStates() below (kept as a no-op stub - it has ~25 live call sites tree-wide, not worth
// touching for zero behavior change), these had no callers to preserve an interface for.

void DXState::checkStates(GLboolean writeAlpha)
{
    // S24: was a GL-context state validator (glGetIntegerv/glIsEnabled read
    // back against sStateMap) with no D3D11 equivalent - removed rather than
    // left dead behind the gDebugGL guard.
    return;
}

///////////////////////////////////////////////////////////////////////

namespace
{
    // Scoped to GL_BLEND/GL_CULL_FACE/GL_SCISSOR_TEST/GL_DEPTH_CLAMP - see
    // DXStateCache.h for why (the only four states LLGLEnable/LLGLDisable
    // actually toggle in this codebase, confirmed by grep, not guessed).
    // Everything else (GL_STENCIL_TEST, GL_POLYGON_OFFSET_*, ...) still
    // updates sStateMap bookkeeping (in setEnabled(), unconditionally) but
    // has no D3D11 equivalent applied yet - deferred until something that
    // toggles them is converted to DX_RENDER.
    void applyDXState(DXenum state, bool enabled)
    {
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        if (state == GL_BLEND)
        {
            // Blend enable is only one of the three pieces D3D11 bundles
            // into one ID3D11BlendState (see DXStateCache.h) - factors and
            // color write mask live on LLRender, not here. Route through
            // the shared chokepoint rather than building a partial state.
            gDX.applyDXBlendState();
        }
        else if (state == GL_CULL_FACE || state == GL_SCISSOR_TEST || state == GL_DEPTH_CLAMP
            || state == GL_POLYGON_OFFSET_FILL || state == GL_POLYGON_OFFSET_LINE)
        {
            // S24: GL_CULL_FACE/GL_SCISSOR_TEST/GL_DEPTH_CLAMP/
            // GL_POLYGON_OFFSET_FILL/LINE all bundle into the same D3D11
            // rasterizer-state object - toggling any one must read the
            // CURRENT value of the others too (via sStateMap, already
            // updated by setEnabled() above) so it doesn't silently clobber
            // them back to a default. Routed through applyDXRasterizerState()
            // (llrender.cpp), which gathers all dimensions fresh each call,
            // rather than duplicating getRasterizerState()+RSSetState() inline.
            gDX.applyDXRasterizerState();
        }
    }

    // GL depth funcs -> D3D11_COMPARISON_FUNC. GL_NEVER is first in both
    // enumerations but the underlying values aren't contiguous/matching, so
    // this is a real lookup, not an arithmetic remap.
    //
    // S24: LESS/LEQUAL <-> GREATER/GEQUAL are swapped from the "obvious"
    // direct mapping - the depth buffer stores near=1.0/far=0.0 (reversed-Z,
    // see kGLtoDXDepthRemap in llrender.cpp), so GL's "passes if closer"
    // (GL_LESS/GL_LEQUAL) needs D3D11's GREATER/GREATER_EQUAL to match.
    // EQUAL/NOTEQUAL/ALWAYS/NEVER are direction-independent.
    D3D11_COMPARISON_FUNC glDepthFuncToDX(DXenum depth_func)
    {
        switch (depth_func)
        {
        case GL_NEVER:    return D3D11_COMPARISON_NEVER;
        case GL_LESS:     return D3D11_COMPARISON_GREATER;
        case GL_EQUAL:    return D3D11_COMPARISON_EQUAL;
        case GL_LEQUAL:   return D3D11_COMPARISON_GREATER_EQUAL;
        case GL_GREATER:  return D3D11_COMPARISON_LESS;
        case GL_NOTEQUAL: return D3D11_COMPARISON_NOT_EQUAL;
        case GL_GEQUAL:   return D3D11_COMPARISON_LESS_EQUAL;
        case GL_ALWAYS:   return D3D11_COMPARISON_ALWAYS;
        default:
            LL_WARNS("RenderState") << "Unmapped GL depth func 0x" << std::hex << depth_func << std::dec << LL_ENDL;
            return D3D11_COMPARISON_GREATER_EQUAL;
        }
    }

    // Mirrors applyDXState() above but for DXDepthTest (llglstates.h) -
    // depth-enable/write-enable/depth-func are three independently-set GL
    // toggles (glEnable(GL_DEPTH_TEST)/glDepthMask()/glDepthFunc()) that
    // D3D11 bundles into one ID3D11DepthStencilState (see DXStateCache.h).
    // DXDepthTest already tracks all three as one unit internally
    // (sDepthEnabled/sWriteEnabled/sDepthFunc), so unlike blend state there's
    // no separate LLRender-side chokepoint needed - this is called directly
    // from the constructor/destructor with the full combination in hand.
    void applyDXDepthStencilState(bool depth_enabled, bool write_enabled, DXenum depth_func)
    {
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        D3D11_COMPARISON_FUNC func = glDepthFuncToDX(depth_func);
        ID3D11DepthStencilState* ds = DXStateCache::getDepthStencilState(depth_enabled, write_enabled, func);
        ctx->OMSetDepthStencilState(ds, 0);
        // KRLV_TOUCHPOINT-adjacent: lets DXStateCache::tagAttachmentStencil()
        // (@camtextures) later derive a matching stencil-enabled variant of
        // THIS exact depth config instead of guessing - see its header
        // comment in DXStateCache.h.
        DXStateCache::recordAppliedDepthStencilState(depth_enabled, write_enabled, func);
    }
}

DXState::DXState(LLGLenum state, S32 enabled) :
    mState(state), mWasEnabled(false), mIsEnabled(false)
{

    if (mState)
    {
        mWasEnabled = sStateMap[state];
        setEnabled(enabled);
    }
}

void DXState::setEnabled(S32 enabled)
{
    if (!mState)
    {
        return;
    }
    if (enabled == CURRENT_STATE)
    {
        enabled = sStateMap[mState] == GL_TRUE ? ENABLED_STATE : DISABLED_STATE;
    }
    else if (enabled == ENABLED_STATE && sStateMap[mState] != GL_TRUE)
    {
        // S24: flush BEFORE the bookkeeping+state-apply, not after -
        // applyDXState() changes real D3D11 pipeline state immediately, and
        // without flushing first, already-queued CPU-side geometry would be
        // drawn with the new state instead of the one it was built under.
        // gDXUIBatch is a second, independent GPU-submission queue
        // gDX.flush() doesn't reach, so flush it too.
        gDX.flush();
        gDXUIBatch.flushPending();
        sStateMap[mState] = GL_TRUE;
        applyDXState(mState, true);
    }
    else if (enabled == DISABLED_STATE && sStateMap[mState] != GL_FALSE)
    {
        // Same missing-flush issue as the ENABLED_STATE branch above -
        // also flush gDXUIBatch, see its comment there.
        gDX.flush();
        gDXUIBatch.flushPending();
        sStateMap[mState] = GL_FALSE;
        applyDXState(mState, false);
    }
    mIsEnabled = enabled;
}

DXState::~DXState()
{
    if (mState)
    {
#if LL_DEBUG_GL
        // S24: GL-context state validator (glIsEnabled read back against
        // driver state) with no D3D11 equivalent - removed.
#endif

        if (mIsEnabled != mWasEnabled)
        {
            // S24: must pair the sStateMap write with a real applyDXState()
            // call and a preceding flush, same as setEnabled() - otherwise
            // this restore path desyncs bookkeeping from real GPU state
            // (was the root cause of a black-world bug: blend state stuck
            // enabled, never actually toggled).
            gDX.flush();
            sStateMap[mState] = mWasEnabled ? GL_TRUE : GL_FALSE;
            applyDXState(mState, mWasEnabled);
        }
    }
}

LLGLCullFace::LLGLCullFace(LLGLenum face) :
    mPrevFace(DXState::getCullFace())
{
    gDX.cullFace(face);
}

LLGLCullFace::~LLGLCullFace()
{
    gDX.cullFace(mPrevFace);
}

////////////////////////////////////////////////////////////////////////////////

void LLGLManager::initGLStates()
{
    //gl states moved to classes in llglstates.h
    DXState::initClass();
}

////////////////////////////////////////////////////////////////////////////////

void parse_gl_version( S32* major, S32* minor, S32* release, std::string* vendor_specific, std::string* version_string )
{
    // GL_VERSION returns a null-terminated string with the format:
    // <major>.<minor>[.<release>] [<vendor specific>]

    const char* version = (const char*) glGetString(GL_VERSION);
    *major = 0;
    *minor = 0;
    *release = 0;
    vendor_specific->assign("");

    if( !version )
    {
        return;
    }

    version_string->assign(version);

    std::string ver_copy( version );
    S32 len = (S32)strlen( version );   /* Flawfinder: ignore */
    S32 i = 0;
    S32 start;
    // Find the major version
    start = i;
    for( ; i < len; i++ )
    {
        if( '.' == version[i] )
        {
            break;
        }
    }
    std::string major_str = ver_copy.substr(start,i-start);
    LLStringUtil::convertToS32(major_str, *major);

    if( '.' == version[i] )
    {
        i++;
    }

    // Find the minor version
    start = i;
    for( ; i < len; i++ )
    {
        if( ('.' == version[i]) || isspace(version[i]) )
        {
            break;
        }
    }
    std::string minor_str = ver_copy.substr(start,i-start);
    LLStringUtil::convertToS32(minor_str, *minor);

    // Find the release number (optional)
    if( '.' == version[i] )
    {
        i++;

        start = i;
        for( ; i < len; i++ )
        {
            if( isspace(version[i]) )
            {
                break;
            }
        }

        std::string release_str = ver_copy.substr(start,i-start);
        LLStringUtil::convertToS32(release_str, *release);
    }

    // Skip over any white space
    while( version[i] && isspace( version[i] ) )
    {
        i++;
    }

    // Copy the vendor-specific string (optional)
    if( version[i] )
    {
        vendor_specific->assign( version + i );
    }
}


void parse_glsl_version(S32& major, S32& minor)
{
    // GL_SHADING_LANGUAGE_VERSION returns a null-terminated string with the format:
    // <major>.<minor>[.<release>] [<vendor specific>]

    const char* version = (const char*) glGetString(GL_SHADING_LANGUAGE_VERSION);
    major = 0;
    minor = 0;

    if( !version )
    {
        return;
    }

    std::string ver_copy( version );
    S32 len = (S32)strlen( version );   /* Flawfinder: ignore */
    S32 i = 0;
    S32 start;
    // Find the major version
    start = i;
    for( ; i < len; i++ )
    {
        if( '.' == version[i] )
        {
            break;
        }
    }
    std::string major_str = ver_copy.substr(start,i-start);
    LLStringUtil::convertToS32(major_str, major);

    if( '.' == version[i] )
    {
        i++;
    }

    // Find the minor version
    start = i;
    for( ; i < len; i++ )
    {
        if( ('.' == version[i]) || isspace(version[i]) )
        {
            break;
        }
    }
    std::string minor_str = ver_copy.substr(start,i-start);
    LLStringUtil::convertToS32(minor_str, minor);
}

LLGLUserClipPlane::LLGLUserClipPlane(const LLPlane& p, const glm::mat4& modelview, const glm::mat4& projection, bool apply)
{
    mApply = apply;

    if (mApply)
    {
        mModelview = modelview;
        mProjection = projection;

        //flip incoming LLPlane to get consistent behavior compared to frustum culling
        setPlane(-p[0], -p[1], -p[2], -p[3]);
    }
}

void LLGLUserClipPlane::disable()
{
    if (mApply)
    {
        gDX.matrixMode(LLRender::MM_PROJECTION);
        gDX.popMatrix();
        gDX.matrixMode(LLRender::MM_MODELVIEW);
    }
    mApply = false;
}

void LLGLUserClipPlane::setPlane(F32 a, F32 b, F32 c, F32 d)
{
    const glm::mat4& P = mProjection;
    const glm::mat4& M = mModelview;

    glm::mat4 invtrans_MVP = glm::transpose(glm::inverse(P * M));
    glm::vec4 oplane(a, b, c, d);
    glm::vec4 cplane = invtrans_MVP * oplane;

    cplane /= fabs(cplane[2]); // Normalize such that depth is not scaled
    cplane[3] -= 1;

    if (cplane[2] < 0)
        cplane *= -1;

    auto suffix = glm::mat4(1.0f);
    suffix[2] = cplane; // Replace the third row with `cplane`

    glm::mat4 newP = suffix * P;

    mProjection = newP;  // Replace gDX's projection matrix manipulation
    mProjectionInverse = glm::transpose(glm::inverse(newP)); // Store the inverse directly
}

LLGLUserClipPlane::~LLGLUserClipPlane()
{
    disable();
}

DXDepthTest::DXDepthTest(bool depth_enabled, bool write_enabled, DXenum depth_func)
: mPrevDepthEnabled(sDepthEnabled), mPrevDepthFunc(sDepthFunc), mPrevWriteEnabled(sWriteEnabled)
{
    // stop_glerror(); // S24: removed - GPU stall in a hot constructor path (created/destroyed hundreds of times per frame).
    checkState();

    if (!depth_enabled)
    { // always disable depth writes if depth testing is disabled
      // GL spec defines this as a requirement, but some implementations allow depth writes with testing disabled
      // The proper way to write to depth buffer with testing disabled is to enable testing and use a depth_func of GL_ALWAYS
        write_enabled = false;
    }

    // S24: applyDXDepthStencilState() calls OMSetDepthStencilState()
    // immediately - without a flush first, geometry already queued under the
    // OLD depth state would be rasterized with the NEW one once it flushes.
    // DXDepthTest backs DXUIDefault (nearly every UI draw call) and is
    // constructed/destroyed hundreds of times per frame, so this is a real,
    // frequent risk.
    if (depth_enabled != sDepthEnabled || depth_func != sDepthFunc || write_enabled != sWriteEnabled)
    {
        // S24: also flush gDXUIBatch's separate pending queue - same
        // missing-flush hazard. Also keeps depth-tested HUD content from
        // merging into the same batch as non-depth-tested screen-space UI.
        gDX.flush();
        gDXUIBatch.flushPending();
        applyDXDepthStencilState(depth_enabled, write_enabled, depth_func);
        sDepthEnabled = depth_enabled;
        sDepthFunc = depth_func;
        sWriteEnabled = write_enabled;
    }
}

DXDepthTest::~DXDepthTest()
{
    checkState();
    // Same missing-flush issue as the constructor above - see its comment.
    if (sDepthEnabled != mPrevDepthEnabled || sDepthFunc != mPrevDepthFunc || sWriteEnabled != mPrevWriteEnabled)
    {
        gDX.flush();
        gDXUIBatch.flushPending();
        applyDXDepthStencilState(mPrevDepthEnabled, mPrevWriteEnabled, mPrevDepthFunc);
        sDepthEnabled = mPrevDepthEnabled;
        sDepthFunc = mPrevDepthFunc;
        sWriteEnabled = mPrevWriteEnabled;
    }
}

void DXDepthTest::checkState()
{
#if LL_DEBUG_GL
    if (gDebugGL)
    {
        GLint func = 0;
        GLboolean mask = GL_FALSE;

        glGetIntegerv(GL_DEPTH_FUNC, &func);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);

        if (glIsEnabled(GL_DEPTH_TEST) != sDepthEnabled ||
            sWriteEnabled != mask ||
            sDepthFunc != func)
        {
            if (gDebugSession)
            {
                gFailLog << "Unexpected depth testing state." << std::endl;
            }
            else
            {
                LL_GL_ERRS << "Unexpected depth testing state." << LL_ENDL;
            }
        }
    }
#endif
}

LLGLSquashToFarClip::LLGLSquashToFarClip()
{
    glm::mat4 proj = get_current_projection();
    setProjectionMatrix(proj, 0);
}

LLGLSquashToFarClip::LLGLSquashToFarClip(const glm::mat4& P, U32 layer)
{
    setProjectionMatrix(P, layer);
}

void LLGLSquashToFarClip::setProjectionMatrix(glm::mat4 projection, U32 layer)
{
    F32 depth = 0.99999f - 0.0001f * layer;

    glm::vec4 P_row_3 = glm::row(projection, 3) * depth;
    projection = glm::row(projection, 2, P_row_3);

    LLRender::eMatrixMode last_matrix_mode = gDX.getMatrixMode();

    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.pushMatrix();
    gDX.loadMatrix(glm::value_ptr(projection));

    gDX.matrixMode(last_matrix_mode);
}

LLGLSquashToFarClip::~LLGLSquashToFarClip()
{
    LLRender::eMatrixMode last_matrix_mode = gDX.getMatrixMode();

    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.popMatrix();

    gDX.matrixMode(last_matrix_mode);
}



LLGLSPipelineSkyBox::LLGLSPipelineSkyBox()
: mCullFace(GL_CULL_FACE)
, mSquashClip()
{
}

LLGLSPipelineSkyBox::~LLGLSPipelineSkyBox()
{
}

LLGLSPipelineDepthTestSkyBox::LLGLSPipelineDepthTestSkyBox(bool depth_test, bool depth_write)
: LLGLSPipelineSkyBox()
, mDepth(depth_test ? GL_TRUE : GL_FALSE, depth_write ? GL_TRUE : GL_FALSE, GL_LEQUAL)
{

}

LLGLSPipelineBlendSkyBox::LLGLSPipelineBlendSkyBox(bool depth_test, bool depth_write)
: LLGLSPipelineDepthTestSkyBox(depth_test, depth_write)
, mBlend(GL_BLEND)
{
    gDX.setSceneBlendType(LLRender::BT_ALPHA);
}

#if LL_WINDOWS
// Expose desired use of high-performance graphics processor to Optimus driver and to AMD driver
// https://docs.nvidia.com/gameworks/content/technologies/desktop/optimus.htm
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif


