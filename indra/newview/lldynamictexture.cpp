/**
 * @file lldynamictexture.cpp
 * @brief Implementation of LLViewerDynamicTexture class
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

#include "llviewerprecompiledheaders.h"

#include "lldynamictexture.h"

// Linden library includes
#include "llglheaders.h"
#include "llwindow.h"           // getPosition()

// Viewer includes
#include "llviewerwindow.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewertexture.h"
#include "llvertexbuffer.h"
#include "llviewerdisplay.h"
#include "llrender.h"
#include "pipeline.h"
#include "llglslshader.h"

#ifdef DX_RENDER
#include "DXContext.h"
#endif

// static
LLViewerDynamicTexture::instance_list_t LLViewerDynamicTexture::sInstances[ LLViewerDynamicTexture::ORDER_COUNT ];
S32 LLViewerDynamicTexture::sNumRenders = 0;

//-----------------------------------------------------------------------------
// LLViewerDynamicTexture()
//-----------------------------------------------------------------------------
LLViewerDynamicTexture::LLViewerDynamicTexture(S32 width, S32 height, S32 components, EOrder order, bool clamp) :
    LLViewerTexture(width, height, components, false),
    mClamp(clamp)
{
    llassert((1 <= components) && (components <= 4));

    generateGLTexture();

    llassert( 0 <= order && order < ORDER_COUNT );
    LLViewerDynamicTexture::sInstances[order].insert(this);
}

//-----------------------------------------------------------------------------
// LLViewerDynamicTexture()
//-----------------------------------------------------------------------------
LLViewerDynamicTexture::~LLViewerDynamicTexture()
{
    for( S32 order = 0; order < ORDER_COUNT; order++ )
    {
        LLViewerDynamicTexture::sInstances[order].erase(this);  // will fail in all but one case.
    }
}

//virtual
S8 LLViewerDynamicTexture::getType() const
{
    return LLViewerTexture::DYNAMIC_TEXTURE ;
}

//-----------------------------------------------------------------------------
// generateGLTexture()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::generateGLTexture()
{
    LLViewerTexture::generateGLTexture() ;
    generateGLTexture(-1, 0, 0, false);
}

void LLViewerDynamicTexture::generateGLTexture(LLGLint internal_format, LLGLenum primary_format, LLGLenum type_format, bool swap_bytes)
{
    if (mComponents < 1 || mComponents > 4)
    {
        LL_ERRS() << "Bad number of components in dynamic texture: " << mComponents << LL_ENDL;
    }

    LLPointer<LLImageRaw> raw_image = new LLImageRaw(mFullWidth, mFullHeight, mComponents);
    if (internal_format >= 0)
    {
        setExplicitFormat(internal_format, primary_format, type_format, swap_bytes);
    }
    createGLTexture(0, raw_image, 0, true, LLGLTexture::DYNAMIC_TEX);
    setAddressMode((mClamp) ? LLTexUnit::TAM_CLAMP : LLTexUnit::TAM_WRAP);
    mGLTexturep->setGLTextureCreated(false);
}

//-----------------------------------------------------------------------------
// render()
//-----------------------------------------------------------------------------
bool LLViewerDynamicTexture::render()
{
    return false;
}

//-----------------------------------------------------------------------------
// preRender()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::preRender(bool clear_depth)
{

     //use the bottom left corner
    mOrigin.set(0, 0);

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    // Set up camera
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    mCamera.setOrigin(*camera);
    mCamera.setAxes(*camera);
    mCamera.setAspect(camera->getAspect());
    mCamera.setView(camera->getView());
    mCamera.setNear(camera->getNear());

    // S24 (2026-08-23): both calls below were completely unguarded raw GL,
    // with no DX_RENDER branch at all - same class of gap as pipeline.cpp's
    // DOF viewport fix (task #145 sweep, ~line 8455) and countless others
    // this session. glViewport() here has ZERO effect under DX_RENDER (no
    // real GL context backs the D3D11 device), so the actual D3D11 viewport
    // stayed at whatever a previous pass left it (typically the full main
    // view) instead of the small mFullWidth x mFullHeight region this
    // dynamic texture is supposed to render into - root cause of Edit
    // Shape/Appearance preview icons (LLVisualParamHint) rendering nothing
    // but their own background, the avatar geometry projected via the
    // wrong viewport landing far outside the tiny region postRender()'s
    // copySubImageFromFrameBuffer() reads back from. mOrigin is always
    // (0,0) here (see comment above) - no cube-face-style Y-flip needed,
    // this isn't reflection-probe capture.
#ifdef DX_RENDER
    gDXContext.setViewport(mOrigin.mX, mOrigin.mY, mFullWidth, mFullHeight);
#else
    glViewport(mOrigin.mX, mOrigin.mY, mFullWidth, mFullHeight);
#endif
    if (clear_depth)
    {
        // Same reasoning - raw glClear() is inert under DX_RENDER. Route
        // through the currently-bound LLRenderTarget's own clear() (set by
        // updateAllInstances() via setBoundTarget() just before preRender()
        // is called), which correctly dispatches to ClearDepthStencilView.
#ifdef DX_RENDER
        if (mBoundTarget)
        {
            mBoundTarget->clear(GL_DEPTH_BUFFER_BIT);
        }
#else
        glClear(GL_DEPTH_BUFFER_BIT);
#endif
    }
}

//-----------------------------------------------------------------------------
// postRender()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::postRender(bool success)
{
    {
        if (success)
        {
            if(mGLTexturep.isNull())
            {
                generateGLTexture() ;
            }
            else if(!mGLTexturep->getHasGLTexture())
            {
                generateGLTexture() ;
            }
            else if(mGLTexturep->getDiscardLevel() != 0)//do not know how it happens, but regenerate one if it does.
            {
                generateGLTexture() ;
            }

            success = mGLTexturep->setSubImageFromFrameBuffer(0, 0, mOrigin.mX, mOrigin.mY, mFullWidth, mFullHeight);
        }
    }

    // restore viewport
    gViewerWindow->setup2DViewport();

    // restore camera
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    camera->setOrigin(mCamera);
    camera->setAxes(mCamera);
    camera->setAspect(mCamera.getAspect());
    camera->setViewNoBroadcast(mCamera.getView());
    camera->setNear(mCamera.getNear());
}

//-----------------------------------------------------------------------------
// static
// updateDynamicTextures()
// Calls update on each dynamic texture.  Calls each group in order: "first," then "middle," then "last."
//-----------------------------------------------------------------------------
bool LLViewerDynamicTexture::updateAllInstances()
{

    sNumRenders = 0;
    if (gGLManager.mIsDisabled)
    {
        return true;
    }

    LLRenderTarget& preview_target = gPipeline.mAuxillaryRT.deferredScreen;
    LLRenderTarget& bake_target = gPipeline.mBakeMap;
    if (!preview_target.isComplete() || !bake_target.isComplete())
    {
        llassert(false);
        return false;
    }
    llassert(preview_target.getWidth() >= LLPipeline::MAX_PREVIEW_WIDTH);
    llassert(preview_target.getHeight() >= LLPipeline::MAX_PREVIEW_WIDTH);
    llassert(bake_target.getWidth() >= (U32) LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH);
    llassert(bake_target.getHeight() >= (U32) LLAvatarAppearanceDefines::SCRATCH_TEX_HEIGHT);

    preview_target.bindTarget();
    preview_target.clear();

    // S24 (2026-08-23): preview_target is gPipeline.mAuxillaryRT.deferredScreen,
    // but binding it here only sets it as the IMMEDIATE draw target for this
    // exact moment - it does nothing to gPipeline.mRT, the pointer that the
    // deferred pipeline's own internal multi-target G-buffer/lighting passes
    // (renderGeomDeferred()/renderGeomPostDeferred(), reached via
    // LLVisualParamHint::render() -> LLPipeline::generateImpostor()) actually
    // read/write through (mRT->deferredScreen, mRT->deferredLight, etc).
    // Without this swap, mRT still pointed at mMainRT (the main game view's
    // RT pack) for the whole duration of every avatar-shape preview icon
    // render (Edit Shape/Appearance floater thumbnails) - the real avatar
    // geometry silently rendered into the MAIN view's buffers instead of
    // mAuxillaryRT, leaving preview_target at nothing but its own clear()
    // color when postRender()'s copySubImageFromFrameBuffer() read it back
    // moments later. Root cause of every preview icon showing a flat black/
    // brown fill instead of the avatar render. Same pattern already used
    // correctly by the sibling GLTF material preview system
    // (llgltfmaterialpreviewmgr.cpp's SetTemporarily<RenderTargetPack*>
    // swap to &gPipeline.mAuxillaryRT) - that system never had this bug.
    // Scoped to only this loop (not the bake_target block below), since
    // LL_TEX_LAYER_SET_BUFFER-style baked-texture compositing doesn't go
    // through the deferred pipeline and has no mRT dependency.
    LLPipeline::RenderTargetPack* saved_rt = gPipeline.mRT;
    gPipeline.mRT = &gPipeline.mAuxillaryRT;

    LLGLSLShader::unbind();
    LLVertexBuffer::unbind();

    bool result = false;
    bool ret = false ;
    auto update_func = [&](LLViewerDynamicTexture* dynamicTexture, LLRenderTarget& renderTarget, S32 width, S32 height)
        {
            if (dynamicTexture->needsRender())
            {
                llassert(dynamicTexture->getFullWidth() <= width);
                llassert(dynamicTexture->getFullHeight() <= height);

                // S24 (2026-08-23): same inert-under-DX_RENDER raw glClear
                // as preRender()'s own depth clear just below - route
                // through the already-bound LLRenderTarget instead.
#ifdef DX_RENDER
                renderTarget.clear(GL_DEPTH_BUFFER_BIT);
#else
                glClear(GL_DEPTH_BUFFER_BIT);
#endif

                gGL.color4f(1.f, 1.f, 1.f, 1.f);
                dynamicTexture->setBoundTarget(&renderTarget);
                dynamicTexture->preRender();    // Must be called outside of startRender()
                result = false;
                if (dynamicTexture->render())
                {
                    ret = true ;
                    result = true;
                    sNumRenders++;
                }
                gGL.flush();
                LLVertexBuffer::unbind();
                dynamicTexture->setBoundTarget(nullptr);
                dynamicTexture->postRender(result);
            }
        };

    // ORDER_FIRST is unused, ORDER_MIDDLE is various ui preview
    for(S32 order = 0; order < ORDER_LAST; ++order)
    {
        for (LLViewerDynamicTexture* dynamicTexture : LLViewerDynamicTexture::sInstances[order])
        {
            update_func(dynamicTexture, preview_target, LLPipeline::MAX_PREVIEW_WIDTH, LLPipeline::MAX_PREVIEW_WIDTH);
        }
    }
    preview_target.flush();

    gPipeline.mRT = saved_rt;

    // ORDER_LAST is baked skin preview, ORDER_RESET resets appearance parameters and does not render.
    bake_target.bindTarget();
    bake_target.clear();

    result = false;
    ret = false;
    for (S32 order = ORDER_LAST; order < ORDER_COUNT; ++order)
    {
        for (LLViewerDynamicTexture* dynamicTexture : LLViewerDynamicTexture::sInstances[order])
        {
            update_func(dynamicTexture, bake_target, LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH, LLAvatarAppearanceDefines::SCRATCH_TEX_HEIGHT);
        }
    }
    bake_target.flush();

    gGL.flush();

    return ret;
}

//-----------------------------------------------------------------------------
// static
// destroyGL()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::destroyGL()
{
    for( S32 order = 0; order < ORDER_COUNT; order++ )
    {
        for (instance_list_t::iterator iter = LLViewerDynamicTexture::sInstances[order].begin();
             iter != LLViewerDynamicTexture::sInstances[order].end(); ++iter)
        {
            LLViewerDynamicTexture *dynamicTexture = *iter;
            dynamicTexture->destroyGLTexture() ;
        }
    }
}

//-----------------------------------------------------------------------------
// static
// restoreGL()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::restoreGL()
{
    if (gGLManager.mIsDisabled)
    {
        return ;
    }

    for( S32 order = 0; order < ORDER_COUNT; order++ )
    {
        for (instance_list_t::iterator iter = LLViewerDynamicTexture::sInstances[order].begin();
             iter != LLViewerDynamicTexture::sInstances[order].end(); ++iter)
        {
            LLViewerDynamicTexture *dynamicTexture = *iter;
            dynamicTexture->restoreGLTexture() ;
        }
    }
}
