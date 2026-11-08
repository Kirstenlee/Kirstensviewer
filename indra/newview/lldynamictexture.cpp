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
#include "dxpipeline.h"
#include "llhlslshader.h"

#include "DXContext.h"

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
    createGLTexture(0, raw_image, 0, true, DXTextureBase::DYNAMIC_TEX);
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

    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    // Set up camera
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    mCamera.setOrigin(*camera);
    mCamera.setAxes(*camera);
    mCamera.setAspect(camera->getAspect());
    mCamera.setView(camera->getView());
    mCamera.setNear(camera->getNear());

    // S24 : Task #256 - mOrigin is always (0,0), but the render-target this
    // draws into (mBoundTarget, set by the caller just before preRender())
    // is usually LARGER than this one dynamic texture's own mFullWidth/
    // mFullHeight - every ORDER_MIDDLE preview (LLVisualParamHint,
    // LLGLTFPreviewTexture, ...) shares one 512x512-or-larger preview_target,
    // same for bake_target. Placing the viewport at Y=0 draws into the TOP
    // rows, but DXTexture::copySubImageFromFrameBuffer()'s postRender()
    // readback (lldynamictexture.cpp's own setSubImageFromFrameBuffer(0, 0,
    // mOrigin.mX, mOrigin.mY, mFullWidth, mFullHeight) call below) computes
    // its source row as (bound-target-height - fb_y - height), i.e. the
    // BOTTOM rows - for a small preview within a larger shared target those
    // are different, never-written, still-cleared-to-transparent rows, so
    // the copy reads back nothing regardless of what actually got drawn.
    // Harmless whenever mFullWidth/mFullHeight already match the bound
    // target's own size (e.g. the full 2048x2048 bake buffers), since top
    // and bottom then coincide - matches S24's own existing bottom-placement
    // convention for a sub-rect readback (dxpipeline.cpp's DoF downsample,
    // llviewerwindow.cpp's screenshot readback). Place the viewport at the
    // bottom of whatever's actually bound instead, so the draw and the
    // later readback agree on which rows they mean.
    //
    // S24 : Task #256 - a flip_y=true viewport (LLViewerWindow::setup3DViewport()'s
    // own convention for the main 3D view) was tried here and made things
    // WORSE (negative-height D3D11 viewport flips effective triangle winding
    // order too, which the main view's pipeline evidently compensates for
    // elsewhere in a way this isolated preview render doesn't - avatar
    // rendered inside-out). Reverted - stay with the plain, unflipped
    // viewport; whatever corrects final orientation needs to happen at
    // readback/display time instead, not by altering the 3D draw itself.
    const S32 viewport_y = mBoundTarget ? ((S32)mBoundTarget->getHeight() - mOrigin.mY - mFullHeight) : mOrigin.mY;
    gDXContext.setViewport(mOrigin.mX, viewport_y, mFullWidth, mFullHeight);
    if (clear_depth)
    {
        // Routed through the currently-bound LLRenderTarget's own clear() (set by
        // updateAllInstances() via setBoundTarget() just before preRender() is called), which
        // correctly dispatches to ClearDepthStencilView.
        if (mBoundTarget)
        {
            mBoundTarget->clear(GL_DEPTH_BUFFER_BIT);
        }
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
    if (gDXDeviceInfo.mIsDisabled)
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
    llassert(preview_target.getWidth() >= DXPipeline::MAX_PREVIEW_WIDTH);
    llassert(preview_target.getHeight() >= DXPipeline::MAX_PREVIEW_WIDTH);
    llassert(bake_target.getWidth() >= (U32) LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH);
    llassert(bake_target.getHeight() >= (U32) LLAvatarAppearanceDefines::SCRATCH_TEX_HEIGHT);

    preview_target.bindTarget();
    preview_target.clear();

    // S24 : Task #256 - this loop used to wrap itself in a ScopedMainRT swap
    // of gPipeline.mRT to mAuxillaryRT, theorizing the deferred pipeline's
    // internal passes (reached via LLVisualParamHint::render() ->
    // DXPipeline::generateImpostor()) needed it. XREF against canonical
    // upstream showed this loop never swaps mRT at all - only
    // llgltfmaterialpreviewmgr.cpp does, scoped locally around its own
    // render call via SetTemporarily<RenderTargetPack*>, matching exactly
    // what this loop's own GLTF preview instances already do themselves
    // (llgltfmaterialpreviewmgr.cpp's generateMaterialPreview() already has
    // its own identical, independently RAII-safe swap). This outer swap was
    // therefore both redundant for GLTF preview AND an S24-only regression
    // for every OTHER ORDER_MIDDLE instance (avatar shape/appearance hints
    // chief among them) that upstream never subjects to it - generateImpostor()
    // ran with gPipeline.mRT pointed at the small, mostly-unpopulated
    // mAuxillaryRT pack instead of the real mMainRT pack it expects.
    LLHLSLShader::unbind();
    LLVertexBuffer::unbind();

    bool result = false;
    bool ret = false ;
    auto update_func = [&](LLViewerDynamicTexture* dynamicTexture, LLRenderTarget& renderTarget, S32 width, S32 height)
        {
            if (dynamicTexture->needsRender())
            {
                llassert(dynamicTexture->getFullWidth() <= width);
                llassert(dynamicTexture->getFullHeight() <= height);

                // Routed through the already-bound LLRenderTarget instead of raw glClear().
                renderTarget.clear(GL_DEPTH_BUFFER_BIT);

                gDX.color4f(1.f, 1.f, 1.f, 1.f);
                dynamicTexture->setBoundTarget(&renderTarget);
                dynamicTexture->preRender();    // Must be called outside of startRender()
                result = false;
                if (dynamicTexture->render())
                {
                    ret = true ;
                    result = true;
                    sNumRenders++;
                }
                gDX.flush();
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
            update_func(dynamicTexture, preview_target, DXPipeline::MAX_PREVIEW_WIDTH, DXPipeline::MAX_PREVIEW_WIDTH);
        }
    }
    preview_target.flush();

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

    gDX.flush();

    return ret;
}

//-----------------------------------------------------------------------------
// static
// destroyDX()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::destroyDX()
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
// restoreDX()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::restoreDX()
{
    if (gDXDeviceInfo.mIsDisabled)
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
