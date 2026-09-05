/**
 * @file llgl.h
 * @brief LLGL definition
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

#ifndef LL_LLGL_H
#define LL_LLGL_H

// This file contains various stuff for handling gl extensions and other gl related stuff.

#include <functional>
#include <string>
#include <unordered_map>
#include <list>

#include "llerror.h"
#include "v4color.h"
#include "llstring.h"
#include "stdtypes.h"
#include "v4math.h"
#include "llplane.h"
#include "llgltypes.h"
#include "llinstancetracker.h"

#include "llglheaders.h"
#include "glm/mat4x4.hpp"

extern bool gDebugGL;
extern bool gDebugSession;
extern llofstream gFailLog;

#define LL_GL_ERRS LL_ERRS("RenderState")

void ll_init_fail_log(std::string filename);

void ll_fail(std::string msg);

void ll_close_fail_log();

class LLSD;

// Manage GL extensions...
class LLGLManager
{
public:
    LLGLManager();

    bool initGL();
    void shutdownGL();

    // S24 (2026-08-05): initGL() above is pure OpenGL (glGetString/WGL/AMD/
    // NVX extension queries) and is never called at all - switchContext()
    // (llwindowwin32.cpp) calls initDX11Context() instead, which never
    // touches mGLVendor/mGLRenderer/mGLVersion/mIsNVIDIA/mIsAMD/mIsIntel/
    // mVRAM/mGLVendorShort, leaving them at their zero/empty/1.0f
    // constructor defaults for the whole session. This silently starves
    // LLFeatureManager of real GPU data - confirmed root cause of a live
    // bug where LLFeatureManager::applyBaseMasks()'s "mGLVersion < 3.99f"
    // check (always true at the 1.0f default) unconditionally applies the
    // "GL3" feature mask, which explicitly zeroes RenderReflectionsEnabled
    // regardless of actual GPU - masking real user settings as if hardware
    // didn't support them. initGLDX() is the real DXGI-based equivalent,
    // called once from switchContext() right after initDX11Context()
    // succeeds, mirroring initGL()'s own call site.
    bool initGLDX();

    void initWGL(); // Initializes stupid WGL extensions

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
    F32 mMaxAnisotropy = 0.f;
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
    bool mHasAnisotropic = false;

    // Vendor-specific extensions
    bool mHasAMDAssociations = false;
    bool mHasNVXGpuMemoryInfo = false;

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

    // Real-time GPU memory query (returns available VRAM in MB, 0 if unsupported)
    U32 queryAvailableVRAM() const;

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
    void initExtensions();
    void initGLStates();
};

extern LLGLManager gGLManager;

class LLQuaternion;
class LLMatrix4;

void rotate_quat(LLQuaternion& rotation);

void flush_glerror(); // Flush GL errors when we know we're handling them correctly.

void clear_glerror();


// S24 (2026-08-19): do_assert_glerror() (llgl.cpp) already early-returns
// unconditionally before touching any GL call - these two macros are
// GL-error-checking helpers with no D3D11 equivalent (checking HRESULTs is
// a completely different mechanism, not something these could ever do), so
// every one of their ~164+ call sites across newview/llrender was still
// paying for a real function call + branch that's guaranteed to do
// nothing. Gated at the macro itself rather than at each call site - one
// chokepoint fix instead of touching every site individually, true
// zero-cost (compiles to nothing, not just an inert call).
# define stop_glerror() ((void)0)
# define llglassertok() ((void)0)

// stop_glerror is still needed on OS X but has performance implications
// use macro below to conditionally add stop_glerror to non-release builds
// on OS X
#if LL_DARWIN && !LL_RELEASE_FOR_DOWNLOAD
#define STOP_GLERROR stop_glerror()
#else
#define STOP_GLERROR
#endif

// S24 (2026-08-31): llglassertok_always()/assert_glerror()/
// do_assert_glerror()/log_glerror() removed entirely - a follow-up to the
// stop_glerror()/llglassertok() fix above found via the Develop-menu "Start
// Debug GL" audit (task #306). Unlike those two, these had ZERO real
// callers anywhere in the tree (confirmed via grep for the macro name
// itself, not just the function) - not a hot-path cost like stop_glerror
// was, just orphaned dead code left over from whatever last called them.

////////////////////////
//
// Note: U32's are GLEnum's...
//

// This is a class for GL state management

/*
    GL STATE MANAGEMENT DESCRIPTION

    LLGLState and its two subclasses, LLGLEnable and LLGLDisable, manage the current
    enable/disable states of the GL to prevent redundant setting of state within a
    render path or the accidental corruption of what state the next path expects.

    Essentially, wherever you would call glEnable set a state and then
    subsequently reset it by calling glDisable (or vice versa), make an instance of
    LLGLEnable with the state you want to set, and assume it will be restored to its
    original state when that instance of LLGLEnable is destroyed.  It is good practice
    to exploit stack frame controls for optimal setting/unsetting and readability of
    code.  In llglstates.h, there are a collection of helper classes that define groups
    of enables/disables that can cause multiple states to be set with the creation of
    one instance.

    Sample usage:

    //disable lighting for rendering hud objects
    //INCORRECT USAGE
    LLGLEnable blend(GL_BLEND);
    renderHUD();
    LLGLDisable blend(GL_BLEND);

    //CORRECT USAGE
    {
        LLGLEnable blend(GL_BLEND);
        renderHUD();
    }

    If a state is to be set on a conditional, the following mechanism
    is useful:

    {
        LLGLEnable blend(blend_hud ? GL_GL_BLEND: 0);
        renderHUD();
    }

    A LLGLState initialized with a parameter of 0 does nothing.

    LLGLState works by maintaining a map of the current GL states, and ignoring redundant
    enables/disables.  If a redundant call is attempted, it becomes a noop, otherwise,
    it is set in the constructor and reset in the destructor.

    For debugging GL state corruption, running with debug enabled will trigger asserts
    if the existing GL state does not match the expected GL state.

*/

class LLGLState
{
public:
    static void initClass();
    static void restoreGL();

    static void resetTextureStates();
    static void dumpStates();

    // make sure GL blend function, GL states, and GL color mask match
    // what we expect
    //  writeAlpha - whether or not writing to alpha channel is expected
    static void checkStates(GLboolean writeAlpha = GL_TRUE);

    // Needed to know "is GL_BLEND currently enabled" from outside this
    // class (LLRender::applyDXBlendState(), llrender.cpp) - D3D11 bundles
    // blend-enable with the blend function into one state object, unlike
    // GL's two independent toggles, so whichever changes needs to read the
    // other to rebuild the combined state. sStateMap is kept accurate (see
    // setEnabled()); this is a plain accessor, not a friend declaration.
    static bool isEnabled(LLGLenum state) { return sStateMap[state] == GL_TRUE; }

protected:
    static std::unordered_map<LLGLenum, LLGLboolean> sStateMap;

public:
    enum { CURRENT_STATE = -2, DISABLED_STATE = 0, ENABLED_STATE = 1 };
    LLGLState(LLGLenum state, S32 enabled = CURRENT_STATE);
    ~LLGLState();
    void setEnabled(S32 enabled);
    void enable() { setEnabled(ENABLED_STATE); }
    void disable() { setEnabled(DISABLED_STATE); }
protected:
    LLGLenum mState;
    bool mWasEnabled;
    bool mIsEnabled;
};

// New LLGLState class wrappers that don't depend on actual GL flags.
class LLGLEnableBlending : public LLGLState
{
public:
    LLGLEnableBlending(bool enable);
};

class LLGLEnableAlphaReject : public LLGLState
{
public:
    LLGLEnableAlphaReject(bool enable);
};

// Enable with functor
class LLGLEnableFunc : LLGLState
{
public:
    LLGLEnableFunc(LLGLenum state, bool enable, std::function<void()> func)
        : LLGLState(state, enable)
    {
        if (enable)
        {
            func();
        }
    }
};

/// TODO: Being deprecated.
class LLGLEnable : public LLGLState
{
public:
    LLGLEnable(LLGLenum state) : LLGLState(state, ENABLED_STATE) {}
};

/// TODO: Being deprecated.
class LLGLDisable : public LLGLState
{
public:
    LLGLDisable(LLGLenum state) : LLGLState(state, DISABLED_STATE) {}
};

/*
  Store and modify projection matrix to create an oblique
  projection that clips to the specified plane.  Oblique
  projections alter values in the depth buffer, so this
  class should not be used mid-renderpass.

  Restores projection matrix on destruction.
  GL_MODELVIEW_MATRIX is active whenever program execution
  leaves this class.
  Does not stack.
  Caches inverse of projection matrix used in gGLObliqueProjectionInverse
*/
class LLGLUserClipPlane
{
public:

    LLGLUserClipPlane(const LLPlane& plane, const glm::mat4& modelview, const glm::mat4& projection, bool apply = true);
    ~LLGLUserClipPlane();

    void setPlane(F32 a, F32 b, F32 c, F32 d);
    void disable();

private:
    bool mApply;

    glm::mat4 mProjection;
    glm::mat4 mModelview;
    glm::mat4 mProjectionInverse; // S24 - Projection matrix inverse
};

/*
  Modify and load projection matrix to push depth values to far clip plane.

  Restores projection matrix on destruction.
  Saves/restores matrix mode around projection manipulation.
  Does not stack.
*/
class LLGLSquashToFarClip
{
public:
    LLGLSquashToFarClip();
    LLGLSquashToFarClip(const glm::mat4& projection, U32 layer = 0);

    void setProjectionMatrix(glm::mat4 projection, U32 layer);

    ~LLGLSquashToFarClip();
};

/*
    Interface for objects that need periodic GL updates applied to them.
    Used to synchronize GL updates with GL thread.
*/
class LLGLUpdate
{
public:

    static std::list<LLGLUpdate*> sGLQ;

    bool mInQ;
    LLGLUpdate()
        : mInQ(false)
    {
    }
    virtual ~LLGLUpdate()
    {
        if (mInQ)
        {
            std::list<LLGLUpdate*>::iterator iter = std::find(sGLQ.begin(), sGLQ.end(), this);
            if (iter != sGLQ.end())
            {
                sGLQ.erase(iter);
            }
        }
    }
    virtual void updateGL() = 0;
};

const U32 FENCE_WAIT_TIME_NANOSECONDS = 1000;  //1 ms

class LLGLFence
{
public:
    virtual ~LLGLFence()
    {
    }

    virtual void placeFence() = 0;
    virtual bool isCompleted() = 0;
    virtual void wait() = 0;
};

class LLGLSyncFence : public LLGLFence
{
public:
    GLsync mSync;

    LLGLSyncFence();
    virtual ~LLGLSyncFence();

    void placeFence();
    bool isCompleted();
    void wait();
};

extern LLMatrix4 gGLObliqueProjectionInverse;

#include "llglstates.h"

void init_glstates();

void parse_gl_version( S32* major, S32* minor, S32* release, std::string* vendor_specific, std::string* version_string );

extern bool gClothRipple;
extern bool gHeadlessClient;
extern bool gNonInteractive;
// S24 (2026-08-31): renamed from gGLActive - true while llappviewer.cpp is
// inside a region that actively touches the render context (window init/
// cleanup, display(), idle_startup(), idleShutdown()) - see each set site's
// own comment. Currently write-only (no live reads - its one real
// consumer, assert_glerror()'s "GL used while not active" check, was
// removed as dead code in task #306), kept/renamed rather than deleted
// since it's a genuinely useful invariant marker for future DX_RENDER
// validation/assertions, not just legacy GL naming debt.
extern bool gDXActive;

// Deal with changing glext.h definitions for newer SDK versions, specifically
// with MAC OSX 10.5 -> 10.6


#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT GL_DEPTH_ATTACHMENT_EXT
#endif

#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT GL_STENCIL_ATTACHMENT_EXT
#endif

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER GL_FRAMEBUFFER_EXT
#define GL_DRAW_FRAMEBUFFER GL_DRAW_FRAMEBUFFER_EXT
#define GL_READ_FRAMEBUFFER GL_READ_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE_EXT
#define GL_FRAMEBUFFER_UNSUPPORTED GL_FRAMEBUFFER_UNSUPPORTED_EXT
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glBlitFramebuffer glBlitFramebufferEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#define glFramebufferRenderbuffer glFramebufferRenderbufferEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#endif

#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER GL_RENDERBUFFER_EXT
#define glGenRenderbuffers glGenRenderbuffersEXT
#define glBindRenderbuffer glBindRenderbufferEXT
#define glRenderbufferStorage glRenderbufferStorageEXT
#define glRenderbufferStorageMultisample glRenderbufferStorageMultisampleEXT
#define glDeleteRenderbuffers glDeleteRenderbuffersEXT
#endif

#ifndef GL_COLOR_ATTACHMENT
#define GL_COLOR_ATTACHMENT GL_COLOR_ATTACHMENT_EXT
#endif

#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0_EXT
#endif

#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1 GL_COLOR_ATTACHMENT1_EXT
#endif

#ifndef GL_COLOR_ATTACHMENT2
#define GL_COLOR_ATTACHMENT2 GL_COLOR_ATTACHMENT2_EXT
#endif

#ifndef GL_COLOR_ATTACHMENT3
#define GL_COLOR_ATTACHMENT3 GL_COLOR_ATTACHMENT3_EXT
#endif


#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 GL_DEPTH24_STENCIL8_EXT
#endif

#endif // LL_LLGL_H
