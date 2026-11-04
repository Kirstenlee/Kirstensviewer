/**
 * @file DXState.h
 * @brief RAII enable/disable pipeline-state bookkeeping (DXState and its
 * wrapper family), split out of dxrender's root llgl.h into core/ to sit
 * next to DXStateCache - both are fixed-function pipeline-state plumbing
 * subordinate to the per-frame bracket. See project_dxrender_consolidation
 * design notes.
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

#ifndef DX_DXSTATE_H
#define DX_DXSTATE_H

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <list>

#include "llgltypes.h"
#include "glm/mat4x4.hpp"

class LLPlane;
class LLMatrix4;

////////////////////////
//
// Note: U32's are GLEnum's...
//

/*
    GL STATE MANAGEMENT DESCRIPTION

    DXState and its two subclasses, LLGLEnable and LLGLDisable, manage the current
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
    LLGLEnable blend(DX_BLEND);
    renderHUD();
    LLGLDisable blend(DX_BLEND);

    //CORRECT USAGE
    {
        LLGLEnable blend(DX_BLEND);
        renderHUD();
    }

    If a state is to be set on a conditional, the following mechanism
    is useful:

    {
        LLGLEnable blend(blend_hud ? DX_BLEND : 0);
        renderHUD();
    }

    A DXState initialized with a parameter of 0 does nothing.

    DXState works by maintaining a map of the current GL states, and ignoring redundant
    enables/disables.  If a redundant call is attempted, it becomes a noop, otherwise,
    it is set in the constructor and reset in the destructor.

    For debugging GL state corruption, running with debug enabled will trigger asserts
    if the existing GL state does not match the expected GL state.

*/

class DXState
{
public:
    static void initClass();
    static void restoreDX();

    // make sure GL blend function, GL states, and GL color mask match
    // what we expect
    //  writeAlpha - whether or not writing to alpha channel is expected
    static void checkStates(bool writeAlpha = true);

    // Needed to know "is DX_BLEND currently enabled" from outside this
    // class (LLRender::applyDXBlendState(), llrender.cpp) - D3D11 bundles
    // blend-enable with the blend function into one state object, unlike
    // GL's two independent toggles, so whichever changes needs to read the
    // other to rebuild the combined state. sStateMap is kept accurate (see
    // setEnabled()); this is a plain accessor, not a friend declaration.
    static bool isEnabled(LLGLenum state) { return sStateMap[state]; }

    // S24: GL's glCullFace(GL_FRONT/GL_BACK) direction, tracked the same way sStateMap tracks
    // DX_CULL_FACE's enable/disable - D3D11 bundles cull direction into the same rasterizer-state
    // object as everything else applyDXRasterizerState() gathers (llrender.cpp), so whoever calls
    // LLRender::cullFace() needs this readable from outside. Defaults to GL_BACK, matching GL's
    // own default and this codebase's prior assumption (glCullFace() was never called anywhere
    // before LLViewerJoint::render()'s hair/skirt "render inside" pass needed it) - see
    // DXStateCache::getRasterizerState()'s cull_front parameter.
    static LLGLenum getCullFace() { return sCullFace; }
    static void setCullFace(LLGLenum face) { sCullFace = face; }

protected:
    // S24: bool, not LLGLboolean (U8) - pure internal bookkeeping, never
    // passed to a real GL/D3D11 FFI call by pointer, so there's no ABI
    // reason to keep the GL-era storage width.
    static std::unordered_map<LLGLenum, bool> sStateMap;
    static LLGLenum sCullFace;

public:
    enum { CURRENT_STATE = -2, DISABLED_STATE = 0, ENABLED_STATE = 1 };
    DXState(LLGLenum state, S32 enabled = CURRENT_STATE);
    ~DXState();
    void setEnabled(S32 enabled);
    void enable() { setEnabled(ENABLED_STATE); }
    void disable() { setEnabled(DISABLED_STATE); }
protected:
    LLGLenum mState;
    bool mWasEnabled;
    bool mIsEnabled;
};

// New DXState class wrappers that don't depend on actual GL flags.
class LLGLEnableBlending : public DXState
{
public:
    LLGLEnableBlending(bool enable);
};

class LLGLEnableAlphaReject : public DXState
{
public:
    LLGLEnableAlphaReject(bool enable);
};

// Enable with functor
class LLGLEnableFunc : DXState
{
public:
    LLGLEnableFunc(LLGLenum state, bool enable, std::function<void()> func)
        : DXState(state, enable)
    {
        if (enable)
        {
            func();
        }
    }
};

/// TODO: Being deprecated.
class LLGLEnable : public DXState
{
public:
    LLGLEnable(LLGLenum state) : DXState(state, ENABLED_STATE) {}
};

/// TODO: Being deprecated.
class LLGLDisable : public DXState
{
public:
    LLGLDisable(LLGLenum state) : DXState(state, DISABLED_STATE) {}
};

// S24: RAII guard for DXState::sCullFace (GL_FRONT/GL_BACK) - unlike GL's stateless per-call
// glCullFace(), this global has to be tracked explicitly under DX_RENDER (see DXState::
// getCullFace()'s own comment) and was being restored by hand with a second, un-guarded
// LLRender::cullFace(GL_BACK) call at the matching call site (llviewerjoint.cpp) - any early
// return/exception between the two calls would leave it stuck, silently corrupting the cull
// direction of every subsequent draw call that touches the rasterizer state (including
// unrelated objects' shadow-map rendering, via LLRender::setPolygonOffset()) until something
// else happened to call cullFace() again. Matches this file's own LLGLEnable/LLGLDisable
// pattern - always restores on scope exit, regardless of how the scope is left.
class LLGLCullFace
{
public:
    LLGLCullFace(LLGLenum face);
    ~LLGLCullFace();
private:
    LLGLenum mPrevFace;
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
  Caches inverse of projection matrix used in gDXObliqueProjectionInverse
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
    glm::mat4 mProjectionInverse;
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

// S24: LLGLFence/LLGLSyncFence (GL-only fence sync) removed; DX_RENDER's
// equivalent is DXQuery (see llviewerstats.cpp's checkGPUFrameCompletion()).

extern LLMatrix4 gDXObliqueProjectionInverse;

#endif // DX_DXSTATE_H
