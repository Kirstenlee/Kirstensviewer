/**
 * @file dxdrawpoolterrain.cpp
 * @brief Fresh DX11-native implementation of LLDrawPoolTerrain's deferred
 * render path.
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "llviewerprecompiledheaders.h"

#include "dxdrawpoolterrain.h"

#include "lldrawpoolterrain.h"
#include "llfasttimer.h"
#include "llagent.h"
#include "llviewercontrol.h"
#include "lldrawable.h"
#include "llface.h"
#include "llsurface.h"
#include "llsurfacepatch.h"
#include "llviewerregion.h"
#include "llvlcomposition.h"
#include "llviewerparcelmgr.h"
#include "llviewerparceloverlay.h"
#include "llvosurfacepatch.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llrender.h"
#include "llenvironment.h"
#include "llsettingsvo.h"

namespace
{
    LLGLSLShader* sShader = nullptr;

    void drawLoop(LLDrawPoolTerrain& pool)
    {
        if (!pool.mDrawFace.empty())
        {
            for (std::vector<LLFace*>::iterator iter = pool.mDrawFace.begin();
                 iter != pool.mDrawFace.end(); iter++)
            {
                LLFace* facep = *iter;

                llassert(gGL.getMatrixMode() == LLRender::MM_MODELVIEW);
                LLRenderPass::applyModelMatrix(&facep->getDrawable()->getRegion()->mRenderMatrix);

                facep->renderIndexed();
            }
        }
    }

    void boostTerrainDetailTextures(LLDrawPoolTerrain& pool)
    {
        LLViewerRegion* regionp = pool.mDrawFace[0]->getDrawable()->getVObj()->getRegion();
        LLVLComposition* compp = regionp->getComposition();
        compp->boost();
    }

    // S24 (2026-08-06): this used to only ever bind detail_0, to hardcoded
    // unit 0 - a documented workaround from when LLGLSLShader::enableTexture()
    // was a hardcoded -1 no-op under DX_RENDER (phase 5.2). Task #103 (this
    // same session, well before tonight) already gave enableTexture() a
    // real D3D-reflection-based channel mapping, but this function was
    // never updated to actually use it - meaning detail_1/detail_2/detail_3/
    // alpha_ramp were NEVER bound at all, every frame, leaving those 4
    // texture slots holding whatever a completely unrelated, previous draw
    // call happened to leave there. Root cause of a real, reported bug:
    // terrain visibly "grabbing" other scene objects' textures, changing
    // live with camera rotation/freecam (since draw order - i.e. what's
    // left in those slots - depends on what else is visible/culled), and
    // showing solid red on an empty test sim (nothing else drawn first to
    // leave a texture behind). Rewritten to bind all 5 textures for real,
    // mirroring lldrawpoolterrain.cpp's GL renderFullShaderTextures() exactly.
    void renderFullShaderTextures(LLDrawPoolTerrain& pool)
    {
        LLViewerRegion* regionp = pool.mDrawFace[0]->getDrawable()->getVObj()->getRegion();
        LLVLComposition* compp = regionp->getComposition();

        LLViewerTexture* detail_texture0p = compp->getDetailTexture(0);
        LLViewerTexture* detail_texture1p = compp->getDetailTexture(1);
        LLViewerTexture* detail_texture2p = compp->getDetailTexture(2);
        LLViewerTexture* detail_texture3p = compp->getDetailTexture(3);

        LLVector3d region_origin_global = gAgent.getRegion()->getOriginGlobal();
        F32 offset_x = (F32)fmod(region_origin_global.mdV[VX], 1.0 / (F64)LLDrawPoolTerrain::sDetailScale) * LLDrawPoolTerrain::sDetailScale;
        F32 offset_y = (F32)fmod(region_origin_global.mdV[VY], 1.0 / (F64)LLDrawPoolTerrain::sDetailScale) * LLDrawPoolTerrain::sDetailScale;

        LLVector4 tp0, tp1;
        tp0.setVec(LLDrawPoolTerrain::sDetailScale, 0.0f, 0.0f, offset_x);
        tp1.setVec(0.0f, LLDrawPoolTerrain::sDetailScale, 0.0f, offset_y);

        S32 detail0 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0);
        gGL.getTexUnit(detail0)->bind(detail_texture0p);
        gGL.getTexUnit(detail0)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        gGL.getTexUnit(detail0)->activate();

        sShader->uniform4fv(LLShaderMgr::OBJECT_PLANE_S, 1, tp0.mV);
        sShader->uniform4fv(LLShaderMgr::OBJECT_PLANE_T, 1, tp1.mV);

        S32 detail1 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL1);
        gGL.getTexUnit(detail1)->bind(detail_texture1p);
        gGL.getTexUnit(detail1)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        gGL.getTexUnit(detail1)->activate();

        S32 detail2 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL2);
        gGL.getTexUnit(detail2)->bind(detail_texture2p);
        gGL.getTexUnit(detail2)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        gGL.getTexUnit(detail2)->activate();

        S32 detail3 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL3);
        gGL.getTexUnit(detail3)->bind(detail_texture3p);
        gGL.getTexUnit(detail3)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        gGL.getTexUnit(detail3)->activate();

        S32 alpha_ramp = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
        gGL.getTexUnit(alpha_ramp)->bind(pool.m2DAlphaRampImagep);
        gGL.getTexUnit(alpha_ramp)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);

        drawLoop(pool);

        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0);
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL1);
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL2);
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL3);

        gGL.getTexUnit(alpha_ramp)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(alpha_ramp)->disable();
        gGL.getTexUnit(alpha_ramp)->activate();

        gGL.getTexUnit(detail3)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(detail3)->disable();
        gGL.getTexUnit(detail3)->activate();

        gGL.getTexUnit(detail2)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(detail2)->disable();
        gGL.getTexUnit(detail2)->activate();

        gGL.getTexUnit(detail1)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(detail1)->disable();
        gGL.getTexUnit(detail1)->activate();

        gGL.getTexUnit(detail0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(detail0)->enable(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(detail0)->activate();
    }

    void renderFullShaderPBR(LLDrawPoolTerrain& pool, bool use_local_materials)
    {
        LLViewerRegion* regionp = pool.mDrawFace[0]->getDrawable()->getVObj()->getRegion();
        LLVLComposition* compp = regionp->getComposition();
        const LLPointer<LLFetchedGLTFMaterial>* fetched_materials = compp->getDetailRenderMaterials();

        if (use_local_materials)
        {
            fetched_materials = gLocalTerrainMaterials.getDetailRenderMaterials();
        }

        const LLFetchedGLTFMaterial* fetched_material = fetched_materials[0].get();
        LLViewerTexture* detail_basecolor_texturep = fetched_material ? fetched_material->mBaseColorTexture.get() : nullptr;

        gGL.getTexUnit(0)->activate();
        if (detail_basecolor_texturep)
        {
            gGL.getTexUnit(0)->bind(detail_basecolor_texturep);
        }
        else
        {
            gGL.getTexUnit(0)->bind(LLViewerFetchedTexture::sWhiteImagep);
        }
        gGL.getTexUnit(0)->setTextureAddressMode(LLTexUnit::TAM_WRAP);

        drawLoop(pool);

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(0)->enable(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(0)->activate();
    }

    void renderFullShader(LLDrawPoolTerrain& pool)
    {
        const bool use_local_materials = gLocalTerrainMaterials.makeMaterialsReady(true, false);
        LLViewerRegion* regionp = pool.mDrawFace[0]->getDrawable()->getVObj()->getRegion();
        LLVLComposition* compp = regionp->getComposition();
        const bool use_textures = !use_local_materials && (compp->getMaterialType() == LLTerrainMaterials::Type::TEXTURE);

        if (use_textures)
        {
            sShader = &gDeferredTerrainProgram;
            sShader->bind();
            renderFullShaderTextures(pool);
        }
        else
        {
            U32 paint_type = use_local_materials ? gLocalTerrainMaterials.getPaintType() : compp->getPaintType();
            paint_type = llclamp(paint_type, 0, TERRAIN_PAINT_TYPE_COUNT);
            sShader = &gDeferredPBRTerrainProgram[paint_type];
            sShader->bind();
            renderFullShaderPBR(pool, use_local_materials);
        }
    }

    void renderOwnership(LLDrawPoolTerrain& pool)
    {
        LLGLSPipelineAlpha gls_pipeline_alpha;

        llassert(!pool.mDrawFace.empty());

        LLFace* facep = pool.mDrawFace[0];
        LLDrawable* drawablep = facep->getDrawable();
        const LLViewerObject* objectp = drawablep->getVObj();
        const LLVOSurfacePatch* vo_surface_patchp = (LLVOSurfacePatch*)objectp;
        LLSurfacePatch* surface_patchp = vo_surface_patchp->getPatch();
        LLSurface* surfacep = surface_patchp->getSurface();
        LLViewerRegion* regionp = surfacep->getRegion();
        LLViewerParcelOverlay* overlayp = regionp->getParcelOverlay();
        LLViewerTexture* texturep = overlayp->getTexture();

        gGL.getTexUnit(0)->bind(texturep);

        gGL.matrixMode(LLRender::MM_TEXTURE);
        gGL.pushMatrix();

        const F32 TEXTURE_FUDGE = 257.f / 256.f;
        gGL.scalef(TEXTURE_FUDGE, TEXTURE_FUDGE, 1.f);
        for (std::vector<LLFace*>::iterator iter = pool.mDrawFace.begin();
             iter != pool.mDrawFace.end(); iter++)
        {
            LLFace* iter_facep = *iter;
            iter_facep->renderIndexed();
        }

        gGL.matrixMode(LLRender::MM_TEXTURE);
        gGL.popMatrix();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
    }

    void hilightParcelOwners(LLDrawPoolTerrain& pool)
    {
        LLGLSLShader* old_shader = sShader;
        sShader->unbind();
        sShader = &gDeferredHighlightProgram;
        sShader->bind();
        gGL.diffuseColor4f(1, 1, 1, 1);
        LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
        // S24 (2026-08-28, task #242): was skipped entirely ("no DX11
        // runtime equivalent") - now real via LLRender::setPolygonOffset()
        // (llrender.cpp), no #ifdef needed here.
        gGL.setPolygonOffset(-1.0f, -1.0f);

        renderOwnership(pool);
        sShader = old_shader;
        sShader->bind();
    }
}

// static
void DXDrawPoolTerrain::beginDeferredPass(LLDrawPoolTerrain& pool, S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    pool.LLFacePool::beginRenderPass(pass);
}

// static
void DXDrawPoolTerrain::endDeferredPass(LLDrawPoolTerrain& pool, S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    pool.LLFacePool::endRenderPass(pass);
    sShader->unbind();
}

// static
void DXDrawPoolTerrain::renderDeferred(LLDrawPoolTerrain& pool, S32 pass)
{
    (void)pass;
    LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    if (pool.mDrawFace.empty())
    {
        return;
    }

    boostTerrainDetailTextures(pool);

    renderFullShader(pool);

    // S24 (2026-08-23, pre-alpha perf sweep): raw per-frame (per visible
    // terrain pool) gSavedSettings lookup.
    static LLCachedControl<bool> show_parcel_owners(gSavedSettings, "ShowParcelOwners", false);
    if (show_parcel_owners)
    {
        hilightParcelOwners(pool);
    }
}
