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
    LLHLSLShader* sShader = nullptr;

    void drawLoop(LLDrawPoolTerrain& pool)
    {
        if (!pool.mDrawFace.empty())
        {
            for (std::vector<LLFace*>::iterator iter = pool.mDrawFace.begin();
                 iter != pool.mDrawFace.end(); iter++)
            {
                LLFace* facep = *iter;

                llassert(gDX.getMatrixMode() == LLRender::MM_MODELVIEW);
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

    // Must bind all 5 textures (detail_0-3 + alpha_ramp) via enableTexture()'s
    // real channel mapping. Binding only unit 0 leaves the other slots holding
    // whatever a previously-drawn, unrelated object left bound there.
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
        gDX.getTexUnit(detail0)->bind(detail_texture0p);
        gDX.getTexUnit(detail0)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        gDX.getTexUnit(detail0)->activate();

        sShader->uniform4fv(LLShaderMgr::OBJECT_PLANE_S, 1, tp0.mV);
        sShader->uniform4fv(LLShaderMgr::OBJECT_PLANE_T, 1, tp1.mV);

        S32 detail1 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL1);
        gDX.getTexUnit(detail1)->bind(detail_texture1p);
        gDX.getTexUnit(detail1)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        gDX.getTexUnit(detail1)->activate();

        S32 detail2 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL2);
        gDX.getTexUnit(detail2)->bind(detail_texture2p);
        gDX.getTexUnit(detail2)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        gDX.getTexUnit(detail2)->activate();

        S32 detail3 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL3);
        gDX.getTexUnit(detail3)->bind(detail_texture3p);
        gDX.getTexUnit(detail3)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        gDX.getTexUnit(detail3)->activate();

        S32 alpha_ramp = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
        gDX.getTexUnit(alpha_ramp)->bind(pool.m2DAlphaRampImagep);
        gDX.getTexUnit(alpha_ramp)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);

        drawLoop(pool);

        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0);
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL1);
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL2);
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL3);

        gDX.getTexUnit(alpha_ramp)->unbind(LLTexUnit::TT_TEXTURE);
        gDX.getTexUnit(alpha_ramp)->disable();
        gDX.getTexUnit(alpha_ramp)->activate();

        gDX.getTexUnit(detail3)->unbind(LLTexUnit::TT_TEXTURE);
        gDX.getTexUnit(detail3)->disable();
        gDX.getTexUnit(detail3)->activate();

        gDX.getTexUnit(detail2)->unbind(LLTexUnit::TT_TEXTURE);
        gDX.getTexUnit(detail2)->disable();
        gDX.getTexUnit(detail2)->activate();

        gDX.getTexUnit(detail1)->unbind(LLTexUnit::TT_TEXTURE);
        gDX.getTexUnit(detail1)->disable();
        gDX.getTexUnit(detail1)->activate();

        gDX.getTexUnit(detail0)->unbind(LLTexUnit::TT_TEXTURE);
        gDX.getTexUnit(detail0)->enable(LLTexUnit::TT_TEXTURE);
        gDX.getTexUnit(detail0)->activate();
    }

    // Full GLTF PBR terrain path: per-material-slot (0-3) base color/normal/metal-rough/emissive
    // textures, the KHR_texture_transform-derived UV transform, the paint-map/alpha-ramp texture
    // that blends between the 4 slots, and the GLTF material factor uniforms (base color,
    // metallic, roughness, emissive, alpha cutoff) that pbrterrainF.hlsl's main() reads. Ported
    // 1:1 from the GL backout's LLDrawPoolTerrain::renderFullShaderPBR() - this DX port had been
    // left binding only detail_0_base_color and setting no uniforms at all, so paint_map/
    // alpha_ramp (register t0) went unbound and every GLTF factor uniform was left at whatever
    // the previous shader's binding happened to leave in the constant buffer. That silently broke
    // material slots 1-3, the paint/alpha blend, and Develop > Terrain's "Create/Delete Local
    // Paintmap" (the baked paintmap was never actually sampled at draw time).
    void renderFullShaderPBR(LLDrawPoolTerrain& pool, bool use_local_materials)
    {
        LLViewerRegion* regionp = pool.mDrawFace[0]->getDrawable()->getVObj()->getRegion();
        LLVLComposition* compp = regionp->getComposition();
        const LLPointer<LLFetchedGLTFMaterial>* fetched_materials = compp->getDetailRenderMaterials();

        if (use_local_materials)
        {
            fetched_materials = gLocalTerrainMaterials.getDetailRenderMaterials();
        }

        constexpr U32 terrain_material_count = LLVLComposition::ASSET_COUNT;

        const LLGLTFMaterial* materials[terrain_material_count];
        for (U32 i = 0; i < terrain_material_count; ++i)
        {
            materials[i] = fetched_materials[i].get();
            if (!materials[i]) { materials[i] = &LLGLTFMaterial::sDefault; }
        }

        U32 paint_type = use_local_materials ? gLocalTerrainMaterials.getPaintType() : compp->getPaintType();
        paint_type = llclamp(paint_type, 0, TERRAIN_PAINT_TYPE_COUNT);

        S32 detail_basecolor[terrain_material_count];
        S32 detail_normal[terrain_material_count];
        S32 detail_metalrough[terrain_material_count];
        S32 detail_emissive[terrain_material_count];

        for (U32 i = 0; i < terrain_material_count; ++i)
        {
            const LLFetchedGLTFMaterial* fetched_material = fetched_materials[i].get();
            LLViewerTexture* detail_basecolor_texturep = fetched_material ? fetched_material->mBaseColorTexture.get() : nullptr;
            LLViewerTexture* detail_normal_texturep = fetched_material ? fetched_material->mNormalTexture.get() : nullptr;
            LLViewerTexture* detail_metalrough_texturep = fetched_material ? fetched_material->mMetallicRoughnessTexture.get() : nullptr;
            LLViewerTexture* detail_emissive_texturep = fetched_material ? fetched_material->mEmissiveTexture.get() : nullptr;

            detail_basecolor[i] = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_BASE_COLOR + i);
            gDX.getTexUnit(detail_basecolor[i])->bind(detail_basecolor_texturep ? detail_basecolor_texturep : LLViewerFetchedTexture::sWhiteImagep.get());
            gDX.getTexUnit(detail_basecolor[i])->setTextureAddressMode(LLTexUnit::TAM_WRAP);
            gDX.getTexUnit(detail_basecolor[i])->activate();

            if (LLDrawPoolTerrain::sPBRDetailMode >= TERRAIN_PBR_DETAIL_NORMAL)
            {
                detail_normal[i] = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_NORMAL + i);
                gDX.getTexUnit(detail_normal[i])->bind(detail_normal_texturep ? detail_normal_texturep : LLViewerFetchedTexture::sFlatNormalImagep.get());
                gDX.getTexUnit(detail_normal[i])->setTextureAddressMode(LLTexUnit::TAM_WRAP);
                gDX.getTexUnit(detail_normal[i])->activate();
            }

            if (LLDrawPoolTerrain::sPBRDetailMode >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            {
                detail_metalrough[i] = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_METALLIC_ROUGHNESS + i);
                gDX.getTexUnit(detail_metalrough[i])->bind(detail_metalrough_texturep ? detail_metalrough_texturep : LLViewerFetchedTexture::sWhiteImagep.get());
                gDX.getTexUnit(detail_metalrough[i])->setTextureAddressMode(LLTexUnit::TAM_WRAP);
                gDX.getTexUnit(detail_metalrough[i])->activate();
            }

            if (LLDrawPoolTerrain::sPBRDetailMode >= TERRAIN_PBR_DETAIL_EMISSIVE)
            {
                detail_emissive[i] = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_EMISSIVE + i);
                gDX.getTexUnit(detail_emissive[i])->bind(detail_emissive_texturep ? detail_emissive_texturep : LLViewerFetchedTexture::sWhiteImagep.get());
                gDX.getTexUnit(detail_emissive[i])->setTextureAddressMode(LLTexUnit::TAM_WRAP);
                gDX.getTexUnit(detail_emissive[i])->activate();
            }
        }

        // PBR UV origin is the region's Southwest corner; RenderTerrainPBRScale is folded into
        // the KHR_texture_transform scale (only valid because it's uniform and no other
        // transform is applied to the terrain UVs elsewhere).
        LLGLTFMaterial::TextureTransform::PackTight transforms_packed[terrain_material_count];
        for (U32 i = 0; i < terrain_material_count; ++i)
        {
            const LLFetchedGLTFMaterial* fetched_material = fetched_materials[i].get();
            LLGLTFMaterial::TextureTransform transform;
            if (fetched_material)
            {
                transform = fetched_material->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR];
            }
            transform.mScale.mV[VX] *= LLDrawPoolTerrain::sPBRDetailScale;
            transform.mScale.mV[VY] *= LLDrawPoolTerrain::sPBRDetailScale;

            transform.getPackedTight(transforms_packed[i]);
        }
        const U32 transform_param_count = LLGLTFMaterial::TextureTransform::PACK_TIGHT_SIZE * terrain_material_count;
        constexpr U32 vec4_size = 4;
        const U32 transform_vec4_count = (transform_param_count + (vec4_size - 1)) / vec4_size;
        sShader->uniform4fv(LLShaderMgr::TERRAIN_TEXTURE_TRANSFORMS, transform_vec4_count, (F32*)transforms_packed);

        //
        // Alpha ramp or paint map - blends between the 4 detail material slots
        //
        S32 alpha_ramp = -1;
        S32 paint_map = -1;
        if (paint_type == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE)
        {
            alpha_ramp = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
            gDX.getTexUnit(alpha_ramp)->bind(pool.m2DAlphaRampImagep);
            gDX.getTexUnit(alpha_ramp)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
        }
        else if (paint_type == TERRAIN_PAINT_TYPE_PBR_PAINTMAP)
        {
            paint_map = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_PAINTMAP);
            LLViewerTexture* tex_paint_map = use_local_materials ? gLocalTerrainMaterials.getPaintMap() : compp->getPaintMap();
            // No paintmap baked yet - fall back to rendering just material slot 1.
            if (!tex_paint_map) { tex_paint_map = LLViewerTexture::sBlackImagep.get(); }
            gDX.getTexUnit(paint_map)->bind(tex_paint_map);
            gDX.getTexUnit(paint_map)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);

            sShader->uniform1f(LLShaderMgr::REGION_SCALE, regionp->getWidth());
        }

        //
        // GLTF material factor uniforms
        //
        LLColor4 base_color_factors[terrain_material_count];
        F32 metallic_factors[terrain_material_count];
        F32 roughness_factors[terrain_material_count];
        LLColor3 emissive_colors[terrain_material_count];
        F32 minimum_alphas[terrain_material_count];
        for (U32 i = 0; i < terrain_material_count; ++i)
        {
            const LLGLTFMaterial* material = materials[i];

            base_color_factors[i] = material->mBaseColor;
            metallic_factors[i] = material->mMetallicFactor;
            roughness_factors[i] = material->mRoughnessFactor;
            emissive_colors[i] = material->mEmissiveColor;
            // mAlphaCutoff is only valid for ALPHA_MODE_MASK; dividing by transparency lets the
            // shader compare against the alpha value of the texture without needing transparency.
            F32 min_alpha = -0.0f;
            if (material->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK)
            {
                min_alpha = material->mAlphaCutoff / material->mBaseColor.mV[3];
            }
            minimum_alphas[i] = min_alpha;
        }
        sShader->uniform4fv(LLShaderMgr::TERRAIN_BASE_COLOR_FACTORS, terrain_material_count, (F32*)base_color_factors);
        if (LLDrawPoolTerrain::sPBRDetailMode >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        {
            sShader->uniform4f(LLShaderMgr::TERRAIN_METALLIC_FACTORS, metallic_factors[0], metallic_factors[1], metallic_factors[2], metallic_factors[3]);
            sShader->uniform4f(LLShaderMgr::TERRAIN_ROUGHNESS_FACTORS, roughness_factors[0], roughness_factors[1], roughness_factors[2], roughness_factors[3]);
        }
        if (LLDrawPoolTerrain::sPBRDetailMode >= TERRAIN_PBR_DETAIL_EMISSIVE)
        {
            sShader->uniform3fv(LLShaderMgr::TERRAIN_EMISSIVE_COLORS, terrain_material_count, (F32*)emissive_colors);
        }
        sShader->uniform4f(LLShaderMgr::TERRAIN_MINIMUM_ALPHAS, minimum_alphas[0], minimum_alphas[1], minimum_alphas[2], minimum_alphas[3]);

        drawLoop(pool);

        if (paint_type == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE)
        {
            sShader->disableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
            gDX.getTexUnit(alpha_ramp)->unbind(LLTexUnit::TT_TEXTURE);
            gDX.getTexUnit(alpha_ramp)->disable();
            gDX.getTexUnit(alpha_ramp)->activate();
        }
        else if (paint_type == TERRAIN_PAINT_TYPE_PBR_PAINTMAP)
        {
            sShader->disableTexture(LLViewerShaderMgr::TERRAIN_PAINTMAP);
            gDX.getTexUnit(paint_map)->unbind(LLTexUnit::TT_TEXTURE);
            gDX.getTexUnit(paint_map)->disable();
            gDX.getTexUnit(paint_map)->activate();
        }

        for (U32 i = 0; i < terrain_material_count; ++i)
        {
            sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_BASE_COLOR + i);
            gDX.getTexUnit(detail_basecolor[i])->unbind(LLTexUnit::TT_TEXTURE);
            gDX.getTexUnit(detail_basecolor[i])->disable();
            gDX.getTexUnit(detail_basecolor[i])->activate();

            if (LLDrawPoolTerrain::sPBRDetailMode >= TERRAIN_PBR_DETAIL_NORMAL)
            {
                sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_NORMAL + i);
                gDX.getTexUnit(detail_normal[i])->unbind(LLTexUnit::TT_TEXTURE);
                gDX.getTexUnit(detail_normal[i])->disable();
                gDX.getTexUnit(detail_normal[i])->activate();
            }

            if (LLDrawPoolTerrain::sPBRDetailMode >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            {
                sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_METALLIC_ROUGHNESS + i);
                gDX.getTexUnit(detail_metalrough[i])->unbind(LLTexUnit::TT_TEXTURE);
                gDX.getTexUnit(detail_metalrough[i])->disable();
                gDX.getTexUnit(detail_metalrough[i])->activate();
            }

            if (LLDrawPoolTerrain::sPBRDetailMode >= TERRAIN_PBR_DETAIL_EMISSIVE)
            {
                sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_EMISSIVE + i);
                gDX.getTexUnit(detail_emissive[i])->unbind(LLTexUnit::TT_TEXTURE);
                gDX.getTexUnit(detail_emissive[i])->disable();
                gDX.getTexUnit(detail_emissive[i])->activate();
            }
        }
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

        gDX.getTexUnit(0)->bind(texturep);

        gDX.matrixMode(LLRender::MM_TEXTURE);
        gDX.pushMatrix();

        const F32 TEXTURE_FUDGE = 257.f / 256.f;
        gDX.scalef(TEXTURE_FUDGE, TEXTURE_FUDGE, 1.f);
        for (std::vector<LLFace*>::iterator iter = pool.mDrawFace.begin();
             iter != pool.mDrawFace.end(); iter++)
        {
            LLFace* iter_facep = *iter;
            iter_facep->renderIndexed();
        }

        gDX.matrixMode(LLRender::MM_TEXTURE);
        gDX.popMatrix();
        gDX.matrixMode(LLRender::MM_MODELVIEW);
    }

    void hilightParcelOwners(LLDrawPoolTerrain& pool)
    {
        LLHLSLShader* old_shader = sShader;
        sShader->unbind();
        sShader = &gDeferredHighlightProgram;
        sShader->bind();
        gDX.diffuseColor4f(1, 1, 1, 1);
        LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
        // Real via LLRender::setPolygonOffset() (llrender.cpp), no #ifdef needed.
        gDX.setPolygonOffset(-1.0f, -1.0f);

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

    static LLCachedControl<bool> show_parcel_owners(gSavedSettings, "ShowParcelOwners", false);
    if (show_parcel_owners)
    {
        hilightParcelOwners(pool);
    }
}
