/**
 * @file llrendertarget.h
 * @brief Off screen render target abstraction.  Loose wrapper for GL_EXT_framebuffer_objects.
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

#ifndef LL_LLRENDERTARGET_H
#define LL_LLRENDERTARGET_H

// LLRenderTarget is unavailible on the mapserver since it uses FBOs.

#include "llgl.h"
#include "llrender.h"

#ifdef DX_RENDER
#include "DXRenderTarget.h"
#endif

/*
 Wrapper around OpenGL framebuffer objects for use in render-to-texture

 SAMPLE USAGE:

    LLRenderTarget target;

    ...

    //allocate a 256x256 RGBA render target with depth buffer
    target.allocate(256,256,GL_RGBA,TRUE);

    //render to contents of offscreen buffer
    target.bindTarget();
    target.clear();
    ... <issue drawing commands> ...
    target.flush();

    ...

    //use target as a texture
    gGL.getTexUnit(INDEX)->bind(&target);
    ... <issue drawing commands> ...

*/

class LLRenderTarget
{
public:
    // Whether or not to use FBO implementation
    static bool sUseFBO;
    static U32 sBytesAllocated;
    static U32 sCurFBO;
    static U32 sCurResX;
    static U32 sCurResY;


    LLRenderTarget();
    ~LLRenderTarget();

    //allocate resources for rendering
    //must be called before use
    //multiple calls will release previously allocated resources
    // resX - width
    // resY - height
    // color_fmt - GL color format (e.g. GL_RGB)
    // depth - if true, allocate a depth buffer
    // usage - deprecated, should always be TT_TEXTURE
    bool allocate(U32 resx, U32 resy, U32 color_fmt, bool depth = false, LLTexUnit::eTextureType usage = LLTexUnit::TT_TEXTURE, LLTexUnit::eTextureMipGeneration generateMipMaps = LLTexUnit::TMG_NONE);

    //resize existing attachments to use new resolution and color format
    // CAUTION: if the GL runs out of memory attempting to resize, this render target will be undefined
    // DO NOT use for screen space buffers or for scratch space for an image that might be uploaded
    // DO use for render targets that resize often and aren't likely to ruin someone's day if they break
    void resize(U32 resx, U32 resy);

    //point this render target at a particular LLImageGL
    //   Intended usage:
    //      LLRenderTarget target;
    //      target.addColorAttachment(image);
    //      target.bindTarget();
    //      < issue GL calls>
    //      target.flush();
    //      target.releaseColorAttachment();
    //
    // attachment -- LLImageGL to render into
    // use_name -- optional texture name to target instead of attachment->getTexName()
    // NOTE: setColorAttachment and releaseColorAttachment cannot be used in conjuction with
    // addColorAttachment, allocateDepth, resize, etc.
    void setColorAttachment(LLImageGL* attachment, LLGLuint use_name = 0);

    // detach from current color attachment
    void releaseColorAttachment();

    //add color buffer attachment
    //limit of 4 color attachments per render target
    bool addColorAttachment(U32 color_fmt);

    //allocate a depth texture
    bool allocateDepth();

    //share depth buffer with provided render target
    void shareDepthBuffer(LLRenderTarget& target);

    //free any allocated resources
    //safe to call redundantly
    // asserts that this target is not currently bound or present in the RT stack
    void release();

    //bind target for rendering
    //applies appropriate viewport
    //  If an LLRenderTarget is currently bound, stores a reference to that LLRenderTarget
    //  and restores previous binding on flush() (maintains a stack of Render Targets)
    //  Asserts that this target is not currently bound in the stack
    // bind_depth=false (DX_RENDER only - see DXRenderTarget::bindTarget()'s
    // comment) binds color attachments without the depth-stencil view, for
    // passes that need to sample this target's (possibly shared) depth as
    // an SRV in the same draw. No effect under GL - a bound FBO's depth
    // attachment is fixed at allocate()/shareDepthBuffer() time, not
    // per-bindTarget() call, and GL doesn't hazard-check this the way
    // D3D11 does.
    void bindTarget(bool bind_depth = true);

    //clear render targer, clears depth buffer if present,
    //uses scissor rect if in copy-to-texture mode
    // asserts that this target is currently bound
    void clear(U32 mask = 0xFFFFFFFF);

    // DX_RENDER only - see DXRenderTarget::clearColor()'s comment. No GL
    // equivalent needed yet (nothing on the GL side has hit this gap).
    void clearColor(float r, float g, float b, float a);

    //get applied viewport
    void getViewport(S32* viewport);

    //get X resolution
    U32 getWidth() const { return mResX; }

    //get Y resolution
    U32 getHeight() const { return mResY; }

    LLTexUnit::eTextureType getUsage(void) const { return mUsage; }

    U32 getTexture(U32 attachment = 0) const;
    U32 getNumTextures() const;

    U32 getDepth(void) const { return mDepth; }

    void bindTexture(U32 index, S32 channel, LLTexUnit::eTextureFilterOptions filter_options = LLTexUnit::TFO_BILINEAR);

#ifdef DX_RENDER
    // Narrow read-side accessor for DXPipeline's minimal present blit
    // (stage 5 phase 5.5) - see DXRenderTarget's class comment for the
    // larger "sampling a render target as input texture" gap this doesn't
    // attempt to close (that's bindTexture()/getTexture()'s job, still
    // unconverted - needed for real once phase 5.8's lighting pass reads
    // the G-buffer).
    ID3D11ShaderResourceView* getColorSRV(size_t index) const { return mDXRenderTarget.getColorSRV(index); }

    // S24 (DXReadback, 2026-07-25): same idea as getColorSRV() above -
    // needed by LLViewerWindow::rawSnapshot()'s depth-snapshot path
    // (DXReadback::readDepthPixels() reads from the underlying texture,
    // reached via this SRV's GetResource() - see that call site).
    ID3D11ShaderResourceView* getDepthSRV() const { return mDXRenderTarget.getDepthSRV(); }

    // S24 (2026-08-17): raw texture accessor, for callers doing direct
    // CPU<->GPU pixel transfer (DXReadback::readPixels()/writePixels())
    // rather than sampling this target as a shader input. First real
    // caller: KVOpenCL's GPU post-fx effects (kveffects.cpp).
    ID3D11Texture2D* getDXColorTexture(size_t index) const { return mDXRenderTarget.getColorTexture(index); }

    // S24 (2026-08-06): re-issues OMSetRenderTargets on an ALREADY-bound
    // target to change whether its depth-stencil view is attached, without
    // touching the bindTarget()/flush() stack (bindTarget() asserts the
    // target isn't already bound - calling it a second time mid-pass would
    // trip that, even though semantically we just want to flip bind_depth).
    // Needed because DXPipeline::renderDeferredLighting() binds mRT->screen
    // with bind_depth=false for its own fullscreen ambient/local-lights
    // draws (avoids the depth-as-SRV-while-DSV hazard documented on
    // bindTarget()'s own comment), but the real 3D alpha/fullbright/glow
    // geometry drawn into the same target afterward (renderGeomPostDeferred())
    // needs real depth testing against the opaque scene to be occluded by
    // walls/objects in front of it correctly - found via the user reporting
    // "massive alpha ordering issues" (previously-hidden objects showing
    // through walls) the first time this pass actually ran. No GL
    // equivalent needed - GL's FBO depth attachment is fixed at
    // allocate()/shareDepthBuffer() time, not per-bind, so nothing to
    // change here for that backend.
    //
    // S24 (2026-08-15): read_only_depth passthrough to DXRenderTarget::
    // bindTarget() - see that function's own comment. Local lights
    // (DXPipeline::renderDeferredLighting()'s point/spot light loops) need
    // this variant: they depth-test against already-written scene depth
    // (occlusion against walls/objects) while ALSO sampling that same depth
    // as an SRV for world-position reconstruction (getPosition()/getDepth()
    // in pointLightF.hlsl/spotLightF.hlsl) - bind_depth=false left them with
    // no occlusion at all (found via adversarial review, 2026-08-15).
    void rebindWithDepth(bool bind_depth, bool read_only_depth = false) { mDXRenderTarget.bindTarget(bind_depth, read_only_depth); }
#endif

    //flush rendering operations
    //must be called when rendering is complete
    //should be used 1:1 with bindTarget
    // call bindTarget once, do all your rendering, call flush once
    // If an LLRenderTarget was bound when bindTarget was called, binds that RenderTarget for rendering (maintains RT stack)
    // asserts  that this target is currently bound
    void flush();

    //Returns TRUE if target is ready to be rendered into.
    //That is, if the target has been allocated with at least
    //one renderable attachment (i.e. color buffer, depth buffer).
    bool isComplete() const;

    // Returns true if this RenderTarget is bound somewhere in the stack
    bool isBoundInStack() const;

    static LLRenderTarget* getCurrentBoundTarget() { return sBoundTarget; }

    // *HACK
    void swapFBORefs(LLRenderTarget& other);

    static LLRenderTarget* sBoundTarget;

protected:
    U32 mResX;
    U32 mResY;
    std::vector<U32> mTex;
    std::vector<U32> mInternalFormat;
    U32 mFBO;
    LLRenderTarget* mPreviousRT = nullptr;
#ifdef DX_RENDER
    // DX_RENDER's equivalent of mFBO/mTex/mDepth - see DXRenderTarget.h.
    // Only the write side (allocate/bind/clear/flush) is wired; sampling a
    // render target as an input texture (bindTexture()/getTexture(), used
    // by later lighting/post-process passes) is a documented future gap.
    DXRenderTarget mDXRenderTarget;
#endif

    U32 mDepth;
    bool mUseDepth;
    LLTexUnit::eTextureMipGeneration mGenerateMipMaps;
    U32 mMipLevels;

    LLTexUnit::eTextureType mUsage;
};

#endif

