/**
 * @file DXState.cpp
 * @brief DXState implementation, split out of dxrender's root llgl.cpp into
 * core/ alongside DXStateCache. Also carries DXDepthTest's implementation
 * (declared in llglstates.h, not this header) - DXDepthTest shares this
 * file's applyDXState()/glDepthFuncToDX()/applyDXDepthStencilState() helpers
 * (originally anonymous-namespace/file-private), so splitting them apart
 * without moving DXDepthTest too would have broken that internal linkage.
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

#include "DXState.h"
#include "llglstates.h"
#include "llrender.h"
#include "llglheaders.h"
#include "llplane.h"

#include "glm/glm.hpp"
#include <glm/gtc/matrix_access.hpp>
#include "glm/gtc/type_ptr.hpp"

#include "DXStateCache.h"
#include "DXDevice.h"
#include "DXUIBatch.h"

// Static members
std::unordered_map<LLGLenum, bool> DXState::sStateMap;
LLGLenum DXState::sCullFace = GL_BACK; // OpenGL default

bool DXDepthTest::sDepthEnabled = false;
DXenum DXDepthTest::sDepthFunc = DX_LESS; // OpenGL default
bool DXDepthTest::sWriteEnabled = true;

LLMatrix4 gDXObliqueProjectionInverse;

std::list<LLGLUpdate*> LLGLUpdate::sGLQ;

//static
void DXState::initClass()
{
    sStateMap[GL_DITHER] = true;
    // sStateMap[GL_TEXTURE_2D] = true;

    //make sure multisample defaults to disabled
    // S24: GL_MULTISAMPLE isn't one of applyDXState()'s dispatched states (see that function's
    // own comment - only BLEND/CULL_FACE/SCISSOR_TEST/DEPTH_CLAMP/POLYGON_OFFSET_* have a D3D11
    // target), so this sStateMap bookkeeping is all that ever matters under DX_RENDER; MSAA
    // itself is a swap-chain/render-target sample-count property in D3D11, not a per-draw
    // enable/disable.
    sStateMap[GL_MULTISAMPLE] = false;
}

//static
void DXState::restoreDX()
{
    sStateMap.clear();
    initClass();
}

// S24: resetTextureStates()/dumpStates() had zero callers anywhere in the tree (confirmed by
// grep, not assumed) - removed rather than left as unreferenced raw-GL-calling dead code
// (resetTextureStates() had 3 unguarded calls: glGetIntegerv/glClientActiveTexture). Unlike
// checkStates() below (kept as a no-op stub - it has ~25 live call sites tree-wide, not worth
// touching for zero behavior change), these had no callers to preserve an interface for.

void DXState::checkStates(bool writeAlpha)
{
    // S24: was a GL-context state validator (glGetIntegerv/glIsEnabled read
    // back against sStateMap) with no D3D11 equivalent - removed rather than
    // left dead behind the gDebugGL guard.
    return;
}

///////////////////////////////////////////////////////////////////////

namespace
{
    // DX_BLEND/DX_CULL_FACE/DX_SCISSOR_TEST/DX_DEPTH_CLAMP - the four
    // capability constants adopted so far (llgltypes.h) - guarded against
    // drift/typos against the real OpenGL/extension constants they were
    // adopted from, in this one file where both are visible via the real
    // <GL/gl.h> include further up the chain. See the static_assert further
    // below for the matching depth-func guard.
    static_assert(DX_BLEND == GL_BLEND && DX_CULL_FACE == GL_CULL_FACE &&
                  DX_SCISSOR_TEST == GL_SCISSOR_TEST && DX_DEPTH_CLAMP == GL_DEPTH_CLAMP,
                  "DX_* capability constants (llgltypes.h) must match the real GL_* values");

    // Scoped to DX_BLEND/DX_CULL_FACE/DX_SCISSOR_TEST/DX_DEPTH_CLAMP - see
    // DXStateCache.h for why (the only four states LLGLEnable/LLGLDisable
    // actually toggle in this codebase, confirmed by grep, not guessed).
    // Everything else (GL_STENCIL_TEST, GL_POLYGON_OFFSET_*, ...) still
    // updates sStateMap bookkeeping (in setEnabled(), unconditionally) but
    // has no D3D11 equivalent applied yet - deferred until something that
    // toggles them is converted to DX_RENDER.
    void applyDXState(DXenum state, bool enabled)
    {
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        if (state == DX_BLEND)
        {
            // Blend enable is only one of the three pieces D3D11 bundles
            // into one ID3D11BlendState (see DXStateCache.h) - factors and
            // color write mask live on LLRender, not here. Route through
            // the shared chokepoint rather than building a partial state.
            gDX.applyDXBlendState();
        }
        else if (state == DX_CULL_FACE || state == DX_SCISSOR_TEST || state == DX_DEPTH_CLAMP
            || state == GL_POLYGON_OFFSET_FILL || state == GL_POLYGON_OFFSET_LINE)
        {
            // S24: DX_CULL_FACE/DX_SCISSOR_TEST/DX_DEPTH_CLAMP/
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

    // DX_* depth-func constants -> D3D11_COMPARISON_FUNC. DX_NEVER is first in
    // both enumerations but the underlying values aren't contiguous/matching,
    // so this is a real lookup, not an arithmetic remap. Values guarded
    // against drift/typos against the real OpenGL 1.0 spec constants they
    // were adopted from (llgltypes.h) - this is the one file where both are
    // visible together via the real <GL/gl.h> include further up the chain.
    //
    // S24: LESS/LEQUAL <-> GREATER/GEQUAL are swapped from the "obvious"
    // direct mapping - the depth buffer stores near=1.0/far=0.0 (reversed-Z,
    // see kGLtoDXDepthRemap in llrender.cpp), so "passes if closer"
    // (DX_LESS/DX_LEQUAL) needs D3D11's GREATER/GREATER_EQUAL to match.
    // EQUAL/NOTEQUAL/ALWAYS/NEVER are direction-independent.
    static_assert(DX_NEVER == GL_NEVER && DX_LESS == GL_LESS && DX_EQUAL == GL_EQUAL &&
                  DX_LEQUAL == GL_LEQUAL && DX_GREATER == GL_GREATER && DX_NOTEQUAL == GL_NOTEQUAL &&
                  DX_GEQUAL == GL_GEQUAL && DX_ALWAYS == GL_ALWAYS,
                  "DX_* depth-func constants (llgltypes.h) must match the real GL_* values");
    D3D11_COMPARISON_FUNC glDepthFuncToDX(DXenum depth_func)
    {
        switch (depth_func)
        {
        case DX_NEVER:    return D3D11_COMPARISON_NEVER;
        case DX_LESS:     return D3D11_COMPARISON_GREATER;
        case DX_EQUAL:    return D3D11_COMPARISON_EQUAL;
        case DX_LEQUAL:   return D3D11_COMPARISON_GREATER_EQUAL;
        case DX_GREATER:  return D3D11_COMPARISON_LESS;
        case DX_NOTEQUAL: return D3D11_COMPARISON_NOT_EQUAL;
        case DX_GEQUAL:   return D3D11_COMPARISON_LESS_EQUAL;
        case DX_ALWAYS:   return D3D11_COMPARISON_ALWAYS;
        default:
            LL_WARNS("RenderState") << "Unmapped depth func 0x" << std::hex << depth_func << std::dec << LL_ENDL;
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
        enabled = sStateMap[mState] ? ENABLED_STATE : DISABLED_STATE;
    }
    else if (enabled == ENABLED_STATE && !sStateMap[mState])
    {
        // S24: flush BEFORE the bookkeeping+state-apply, not after -
        // applyDXState() changes real D3D11 pipeline state immediately, and
        // without flushing first, already-queued CPU-side geometry would be
        // drawn with the new state instead of the one it was built under.
        // gDXUIBatch is a second, independent GPU-submission queue
        // gDX.flush() doesn't reach, so flush it too.
        gDX.flush();
        gDXUIBatch.flushPending();
        sStateMap[mState] = true;
        applyDXState(mState, true);
    }
    else if (enabled == DISABLED_STATE && sStateMap[mState])
    {
        // Same missing-flush issue as the ENABLED_STATE branch above -
        // also flush gDXUIBatch, see its comment there.
        gDX.flush();
        gDXUIBatch.flushPending();
        sStateMap[mState] = false;
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
            sStateMap[mState] = mWasEnabled;
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
      // The proper way to write to depth buffer with testing disabled is to enable testing and use a depth_func of DX_ALWAYS
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
