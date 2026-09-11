/**
 * @file dxdrawpoolalpha.cpp
 * @brief Fresh DX11-native implementation of LLDrawPoolAlpha's forward-alpha
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

#include "dxdrawpoolalpha.h"

#include "lldrawpoolalpha.h"
#include "llviewercontrol.h"
#include "llfasttimer.h"
#include "llrender.h"
#include "llface.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewerregion.h"
#include "lldrawpoolwater.h"
#include "llspatialpartition.h"
#include "llenvironment.h"
#include "llviewertexture.h"
#include "llvoavatar.h"
#include "gltfscenemanager.h"

extern bool gCubeSnapshot;

namespace
{
    // Mirrors LLDrawPoolAlpha's private per-instance shader/blend-factor
    // members - file-static here since this render path isn't reentrant
    // (single-threaded main render loop, same assumption the GL source
    // makes about its own instance members being reused call to call).
    LLHLSLShader* target_shader = nullptr;
    LLHLSLShader* simple_shader = nullptr;
    LLHLSLShader* fullbright_shader = nullptr;
    LLHLSLShader* emissive_shader = nullptr;
    LLHLSLShader* pbr_emissive_shader = nullptr;
    LLHLSLShader* pbr_shader = nullptr;

    LLRender::eBlendFactor mColorSFactor = LLRender::BF_UNDEF;
    LLRender::eBlendFactor mColorDFactor = LLRender::BF_UNDEF;
    LLRender::eBlendFactor mAlphaSFactor = LLRender::BF_UNDEF;
    LLRender::eBlendFactor mAlphaDFactor = LLRender::BF_UNDEF;

    const F32 MINIMUM_ALPHA = 0.004f; // ~ 1/255
    const F32 MINIMUM_IMPOSTOR_ALPHA = 0.1f;

    // S24 (alpha attachment-order fix, AYAstorm-derived, LGPL v2.1 - adopted
    // per that project's public spec, explicitly released for free adoption
    // by any LL-derived viewer fork, no PR required). Fixes a real bug also
    // present in stock lldrawpoolalpha.cpp (which this file was ported from
    // "exactly"): in POOL_ALPHA_POST_WATER, rigged content (hair) draws
    // first and writes depth, so non-rigged alpha behind it (windows, lace,
    // foliage) fails the depth test before its own fragment shader runs and
    // reverts to raw skybox. A naive "swap the order" fix then breaks
    // non-rigged AVATAR ATTACHMENTS specifically (eyelashes/eyebrows drawn
    // before hair, over-blended into invisibility) - this 3-value filter,
    // used to split the non-rigged pass into a SIM-only sub-pass and an
    // attachment-only sub-pass either side of the rigged pass, is how
    // AYAstorm's refined fix avoids that regression. See
    // DXDrawPoolAlpha::renderPostDeferred()'s dispatch for the actual 3-pass
    // sequence this enables.
    enum AlphaAttachmentFilter
    {
        ATTACHMENT_ALL,   // default - every existing call site, unchanged behavior
        ATTACHMENT_NONE,  // skip avatar-attachment content (SIM-only sub-pass)
        ATTACHMENT_ONLY   // skip everything except avatar-attachment content
    };

    void prepare_alpha_shader(LLHLSLShader* shader, bool deferredEnvironment, F32 water_sign)
    {
        static LLCachedControl<F32> displayGamma(gSavedSettings, "RenderDeferredDisplayGamma");
        F32 gamma = displayGamma;

        static LLStaticHashedString waterSign("waterSign");

        if (deferredEnvironment)
        {
            shader->mCanBindFast = false;
        }

        shader->bind();
        shader->uniform1f(LLShaderMgr::DISPLAY_GAMMA, (gamma > 0.1f) ? 1.0f / gamma : (1.0f / 2.2f));

        if (LLPipeline::sRenderingHUDs)
        {
            LLVector4 near_clip(0, 0, -1, 0);
            shader->uniform1f(waterSign, 1.f);
            shader->uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, near_clip.mV);
        }
        else
        {
            shader->uniform1f(waterSign, water_sign);
            shader->uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, LLDrawPoolAlpha::sWaterPlane.mV);
        }

        if (LLPipeline::sImpostorRender)
        {
            shader->setMinimumAlpha(MINIMUM_IMPOSTOR_ALPHA);
        }
        else
        {
            shader->setMinimumAlpha(MINIMUM_ALPHA);
        }

        // S24 (2026-08-09, task #170): now that DXVertexLayout supports
        // MAP_WEIGHT4 (task #168), rigged batches are no longer skipped -
        // also prime the rigged variant, matching the real GL source
        // (lldrawpoolalpha.cpp's prepare_alpha_shader()) exactly.
        if (shader->mRiggedVariant && shader->mRiggedVariant != shader)
        {
            prepare_alpha_shader(shader->mRiggedVariant, deferredEnvironment, water_sign);
        }
    }

    // S24 (2026-08-09, task #142): the indexed/simple-texture diffuse bind
    // below (draw->mTextureList / draw->mTexture, the "not a real material"
    // branch every non-GLTF alpha face with no bound normal/spec map takes -
    // glass, plants, particles, hair, most everyday alpha content) used to
    // bind unconditionally at gDX.getTexUnit(0)/getTexUnit(i), mirroring
    // GL's own code verbatim. That's correct under GL - tex0's sampler
    // uniform is set to texture image unit 0 by convention regardless of
    // where in the GLSL source it's declared. It is NOT correct under
    // DX_RENDER: llshadermgr.cpp's dynamic diffuseLookup() HLSL generation
    // (loadShaderFile(), texture_index_channels > 0 branch) places tex0..N
    // at register t5.. (not t0..) whenever the shader also attaches
    // deferredUtil.hlsl (kIndexedTexRegisterBase, gated on
    // isDeferred || hasReflectionProbes - true for every alpha shader this
    // pool binds) to avoid colliding with deferredUtil.hlsl's own t0-t3
    // G-buffer/depth samplers. Binding the actual diffuse SRV to slot 0
    // left the shader's real t5 slot holding whatever an unrelated earlier
    // draw call left there - sampled as (0,0,0,0), so every alpha face's
    // diffuse alpha read as 0 and either discarded (USE_VERTEX_COLOR's
    // minimum_alpha check) or blended fully transparent - real root cause
    // of "alpha renders nothing, toggling render types changes nothing"
    // (not a gating/blend-state bug - every face WAS drawing, just
    // invisibly). Mirrors llshadermgr.cpp's exact kIndexedTexRegisterBase
    // formula so the two stay in lockstep.
    S32 indexedTexRegisterBase(LLHLSLShader* shader)
    {
        return (shader && (shader->mFeatures.isDeferred || shader->mFeatures.hasReflectionProbes)) ? 5 : 0;
    }

    bool texSetup(LLDrawInfo* draw, bool use_material)
    {
        bool tex_setup = false;

        if (draw->mGLTFMaterial)
        {
            if (draw->mTextureMatrix)
            {
                tex_setup = true;
                gDX.getTexUnit(0)->activate();
                gDX.matrixMode(LLRender::MM_TEXTURE);
                gDX.loadMatrix((F32*)draw->mTextureMatrix->mMatrix);
                gPipeline.mTextureMatrixOps++;
            }
        }
        else
        {
            LLHLSLShader* current_shader = LLHLSLShader::sCurBoundShaderPtr;

            if (!LLPipeline::sRenderingHUDs && use_material && current_shader)
            {
                if (draw->mNormalMap)
                {
                    current_shader->bindTexture(LLShaderMgr::BUMP_MAP, draw->mNormalMap);
                }

                if (draw->mSpecularMap)
                {
                    current_shader->bindTexture(LLShaderMgr::SPECULAR_MAP, draw->mSpecularMap);
                }
            }
            else if (current_shader == simple_shader || current_shader == simple_shader->mRiggedVariant)
            {
                current_shader->bindTexture(LLShaderMgr::BUMP_MAP, LLViewerFetchedTexture::sFlatNormalImagep);
                current_shader->bindTexture(LLShaderMgr::SPECULAR_MAP, LLViewerFetchedTexture::sWhiteImagep);
            }

            const S32 indexed_base = indexedTexRegisterBase(current_shader);

            if (draw->mTextureList.size() > 1)
            {
                for (U32 i = 0; i < draw->mTextureList.size(); ++i)
                {
                    if (draw->mTextureList[i].notNull())
                    {
                        gDX.getTexUnit(indexed_base + i)->bindFast(draw->mTextureList[i]);
                    }
                    else
                    {
                        // S24 (2026-08-06): the single-texture branch below
                        // (mTexture.notNull() == false) already unbinds on
                        // null - this loop didn't, leaving channel i holding
                        // whatever an unrelated EARLIER draw call bound there
                        // (e.g. terrain's detail_0-3/alpha_ramp, channels 0-4,
                        // drawn earlier in the same frame via
                        // renderGeomDeferred() before this post-deferred
                        // pool runs) - found via a real reported symptom
                        // (alpha mesh showing a grey/black blend-pattern
                        // texture matching terrain's alpha_ramp look).
                        gDX.getTexUnit(indexed_base + i)->unbindFast(LLTexUnit::TT_TEXTURE);
                    }
                }
            }
            else
            {
                if (draw->mTexture.notNull())
                {
                    if (use_material)
                    {
                        current_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, draw->mTexture);
                    }
                    else
                    {
                        gDX.getTexUnit(indexed_base)->bindFast(draw->mTexture);
                    }

                    if (draw->mTextureMatrix)
                    {
                        tex_setup = true;
                        gDX.getTexUnit(0)->activate();
                        gDX.matrixMode(LLRender::MM_TEXTURE);
                        gDX.loadMatrix((F32*)draw->mTextureMatrix->mMatrix);
                        gPipeline.mTextureMatrixOps++;
                    }
                }
                else
                {
                    gDX.getTexUnit(indexed_base)->unbindFast(LLTexUnit::TT_TEXTURE);
                }
            }
        }

        return tex_setup;
    }

    void restoreTexSetup(bool tex_setup)
    {
        if (tex_setup)
        {
            gDX.getTexUnit(0)->activate();
            gDX.matrixMode(LLRender::MM_TEXTURE);
            gDX.loadIdentity();
            gDX.matrixMode(LLRender::MM_MODELVIEW);
        }
    }

    void drawEmissive(LLDrawInfo* draw)
    {
        LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, 1.f);
        draw->mVertexBuffer->setBuffer();
        draw->mVertexBuffer->drawRange(LLRender::TRIANGLES, draw->mStart, draw->mEnd, draw->mCount, draw->mOffset);
    }

    void renderEmissives(std::vector<LLDrawInfo*>& emissives)
    {
        emissive_shader->bind();
        emissive_shader->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, 1.f);

        for (LLDrawInfo* draw : emissives)
        {
            bool tex_setup = texSetup(draw, false);
            drawEmissive(draw);
            restoreTexSetup(tex_setup);
        }
    }

    void renderPbrEmissives(std::vector<LLDrawInfo*>& emissives)
    {
        pbr_emissive_shader->bind();

        for (LLDrawInfo* draw : emissives)
        {
            llassert(draw->mGLTFMaterial);
            LLGLDisable cull_face(draw->mGLTFMaterial->mDoubleSided ? GL_CULL_FACE : 0);
            draw->mGLTFMaterial->bind(draw->mTexture);
            draw->mVertexBuffer->setBuffer();
            draw->mVertexBuffer->drawRange(LLRender::TRIANGLES, draw->mStart, draw->mEnd, draw->mCount, draw->mOffset);
        }
    }

    // S24 (2026-08-09, task #170): rigged counterparts of renderEmissives()/
    // renderPbrEmissives() - ported directly from lldrawpoolalpha.cpp,
    // no DX-specific changes needed (LLGLDepthTest, uploadMatrixPalette(),
    // setBuffer()/drawRange() are all already DX-safe by composition).
    void renderRiggedEmissives(std::vector<LLDrawInfo*>& emissives)
    {
        LLGLDepthTest depth(GL_TRUE, GL_FALSE); // disable depth writes since "emissive" is additive so sorting doesn't matter
        LLHLSLShader* shader = emissive_shader->mRiggedVariant;
        shader->bind();
        shader->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, 1.f);

        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;

        for (LLDrawInfo* draw : emissives)
        {
            if (LLRenderPass::uploadMatrixPalette(draw->mAvatar, draw->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                bool tex_setup = texSetup(draw, false);
                drawEmissive(draw);
                restoreTexSetup(tex_setup);
            }
        }
    }

    void renderRiggedPbrEmissives(std::vector<LLDrawInfo*>& emissives)
    {
        LLGLDepthTest depth(GL_TRUE, GL_FALSE); // disable depth writes since "emissive" is additive so sorting doesn't matter
        pbr_emissive_shader->bind(true);

        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;

        for (LLDrawInfo* draw : emissives)
        {
            if (!LLRenderPass::uploadMatrixPalette(draw->mAvatar, draw->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                continue;
            }

            LLGLDisable cull_face(draw->mGLTFMaterial->mDoubleSided ? GL_CULL_FACE : 0);
            draw->mGLTFMaterial->bind(draw->mTexture);
            draw->mVertexBuffer->setBuffer();
            draw->mVertexBuffer->drawRange(LLRender::TRIANGLES, draw->mStart, draw->mEnd, draw->mCount, draw->mOffset);
        }
    }

    // S24 (2026-08-09, task #170): now a real 2-pass (static + rigged) loop,
    // matching lldrawpoolalpha.cpp's renderAlphaHighlight() exactly.
    void renderAlphaHighlight()
    {
        for (int pass = 0; pass < 2; ++pass)
        { // two passes, one rigged and one not
            const LLVOAvatar* lastAvatar = nullptr;
            U64 lastMeshId = 0;
            bool skipLastSkin = false;

            LLCullResult::sg_iterator begin = pass == 0 ? gPipeline.beginAlphaGroups() : gPipeline.beginRiggedAlphaGroups();
            LLCullResult::sg_iterator end = pass == 0 ? gPipeline.endAlphaGroups() : gPipeline.endRiggedAlphaGroups();

            for (LLCullResult::sg_iterator i = begin; i != end; ++i)
            {
                LLSpatialGroup* group = *i;
                if (group->getSpatialPartition()->mRenderByGroup && !group->isDead())
                {
                    LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[LLRenderPass::PASS_ALPHA + pass]; // <-- hacky + pass to use PASS_ALPHA_RIGGED on second pass

                    for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)
                    {
                        LLDrawInfo& params = **k;

                        bool rigged = (params.mAvatar != nullptr);
                        gHighlightProgram.bind(rigged);

                        if (rigged)
                        {
                            if (!LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                            {
                                continue;
                            }
                        }

                        gDX.diffuseColor4f(1, 0, 0, 1);
                        LLRenderPass::applyModelMatrix(params);
                        params.mVertexBuffer->setBuffer();
                        params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
                    }
                }
            }
        }

        // make sure static version of highlight shader is bound before returning
        gHighlightProgram.bind();
    }

    void renderDebugAlpha(LLDrawPoolAlpha& pool)
    {
        if (LLDrawPoolAlpha::sShowDebugAlpha && !gCubeSnapshot && !LLPipeline::sReflectionRender)
        {
            gHighlightProgram.bind();
            gDX.diffuseColor4f(1, 0, 0, 1);
            gDX.getTexUnit(0)->bindFast(LLViewerFetchedTexture::getSmokeImage());

            renderAlphaHighlight();

            pool.pushUntexturedBatches(LLRenderPass::PASS_ALPHA_MASK);
            pool.pushUntexturedBatches(LLRenderPass::PASS_ALPHA_INVISIBLE);

            gDX.diffuseColor4f(0, 0, 1, 1);
            pool.pushUntexturedBatches(LLRenderPass::PASS_MATERIAL_ALPHA_MASK);
            pool.pushUntexturedBatches(LLRenderPass::PASS_NORMMAP_MASK);
            pool.pushUntexturedBatches(LLRenderPass::PASS_SPECMAP_MASK);
            pool.pushUntexturedBatches(LLRenderPass::PASS_NORMSPEC_MASK);
            pool.pushUntexturedBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK);
            pool.pushUntexturedBatches(LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK);

            gDX.diffuseColor4f(0, 1, 0, 1);
            pool.pushUntexturedBatches(LLRenderPass::PASS_INVISIBLE);

            // S24 (2026-08-09, task #170): rigged half, ported directly.
            gHighlightProgram.mRiggedVariant->bind();
            gDX.diffuseColor4f(1, 0, 0, 1);

            pool.pushRiggedBatches(LLRenderPass::PASS_ALPHA_MASK_RIGGED, false);
            pool.pushRiggedBatches(LLRenderPass::PASS_ALPHA_INVISIBLE_RIGGED, false);

            gDX.diffuseColor4f(0, 0, 1, 1);
            pool.pushRiggedBatches(LLRenderPass::PASS_MATERIAL_ALPHA_MASK_RIGGED, false);
            pool.pushRiggedBatches(LLRenderPass::PASS_NORMMAP_MASK_RIGGED, false);
            pool.pushRiggedBatches(LLRenderPass::PASS_SPECMAP_MASK_RIGGED, false);
            pool.pushRiggedBatches(LLRenderPass::PASS_NORMSPEC_MASK_RIGGED, false);
            pool.pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK_RIGGED, false);
            pool.pushRiggedBatches(LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK_RIGGED, false);

            gDX.diffuseColor4f(0, 1, 0, 1);
            pool.pushRiggedBatches(LLRenderPass::PASS_INVISIBLE_RIGGED, false);

            LLHLSLShader::sCurBoundShaderPtr->unbind();
        }
    }

    // S24 (2026-08-09, task #170): now takes `rigged`, matching
    // lldrawpoolalpha.cpp's renderAlpha(mask, depth_only, rigged) exactly -
    // called once with rigged=false (static geometry, PASS_ALPHA /
    // beginAlphaGroups()) and once with rigged=true (rigged mesh
    // attachments/clothing/mesh bodies, PASS_ALPHA_RIGGED /
    // beginRiggedAlphaGroups()) from forwardRender() below. This is the
    // real fix for task #157 (invisible avatar mesh bodies/clothing) - the
    // vertex-layout wall (task #168) and POOL_AVATAR whitelist (task #169)
    // were necessary but not sufficient; rigged batches never reached a
    // draw call at all until now.
    void renderAlpha(LLDrawPoolAlpha& pool, U32 mask, bool depth_only, bool rigged, AlphaAttachmentFilter filter = ATTACHMENT_ALL)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
        bool initialized_lighting = false;
        bool light_enabled = true;

        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        const LLHLSLShader* lastAvatarShader = nullptr;
        bool skipLastSkin = false;

        // S24 (2026-09-09, perf pass): sentinels outside each field's real
        // range, matching dxdrawpoolmaterials.cpp's identical pattern - avoid
        // re-uploading these 3 per-drawable uniforms (each a hashed
        // mConstants lookup + write in DXShader::setUniformFloatArray(), x2
        // for vertex+pixel stage) when the value hasn't actually changed
        // since the last draw item. Reset to the sentinel whenever the bound
        // shader changes (below) so the newly-bound shader's own constant
        // buffer still gets a real first set.
        LLVector4 lastSpecColor(-1.f, -1.f, -1.f, -1.f);
        F32 lastEnvIntensity = -1.f;
        F32 lastBrightness = -1.f;

        LLCullResult::sg_iterator begin;
        LLCullResult::sg_iterator end;

        if (rigged)
        {
            begin = gPipeline.beginRiggedAlphaGroups();
            end = gPipeline.endRiggedAlphaGroups();
        }
        else
        {
            begin = gPipeline.beginAlphaGroups();
            end = gPipeline.endAlphaGroups();
        }

        LLEnvironment& env = LLEnvironment::instance();
        F32 water_height = env.getWaterHeight();

        bool above_water = pool.getType() == LLDrawPool::POOL_ALPHA_POST_WATER;
        if (LLPipeline::sUnderWaterRender)
        {
            above_water = !above_water;
        }

        for (LLCullResult::sg_iterator i = begin; i != end; ++i)
        {
            LLSpatialGroup* group = *i;
            llassert(group);
            llassert(group->getSpatialPartition());

            if (group->getSpatialPartition()->mRenderByGroup && !group->isDead())
            {
                LLSpatialBridge* bridge = group->getSpatialPartition()->asBridge();
                const LLVector4a* ext = bridge ? bridge->getSpatialExtents() : group->getExtents();

                if (!LLPipeline::sRenderingHUDs)
                {
                    if (above_water)
                    {
                        if (ext[1].getF32ptr()[2] < water_height)
                        {
                            continue;
                        }
                    }
                    else
                    {
                        if (ext[0].getF32ptr()[2] > water_height)
                        {
                            continue;
                        }
                    }
                }

                static std::vector<LLDrawInfo*> emissives;
                static std::vector<LLDrawInfo*> rigged_emissives;
                static std::vector<LLDrawInfo*> pbr_emissives;
                static std::vector<LLDrawInfo*> pbr_rigged_emissives;

                emissives.resize(0);
                rigged_emissives.resize(0);
                pbr_emissives.resize(0);
                pbr_rigged_emissives.resize(0);

                bool is_particle_or_hud_particle = group->getSpatialPartition()->mPartitionType == LLViewerRegion::PARTITION_PARTICLE
                                                          || group->getSpatialPartition()->mPartitionType == LLViewerRegion::PARTITION_HUD_PARTICLE;

                bool disable_cull = is_particle_or_hud_particle;
                LLGLDisable cull(disable_cull ? GL_CULL_FACE : 0);

                LLSpatialGroup::drawmap_elem_t& draw_info = rigged ? group->mDrawMap[LLRenderPass::PASS_ALPHA_RIGGED] : group->mDrawMap[LLRenderPass::PASS_ALPHA];

                for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)
                {
                    LLDrawInfo& params = **k;
                    if ((bool)params.mAvatar != rigged)
                    {
                        continue;
                    }

                    // S24 (alpha attachment-order fix) - see this file's
                    // AlphaAttachmentFilter comment. No-op when filter is
                    // the default ATTACHMENT_ALL.
                    if (filter == ATTACHMENT_NONE && params.mAttachedToAvatar)
                    {
                        continue;
                    }
                    if (filter == ATTACHMENT_ONLY && !params.mAttachedToAvatar)
                    {
                        continue;
                    }

                    LLRenderPass::applyModelMatrix(params);

                    LLMaterial* mat = nullptr;
                    LLGLTFMaterial* gltf_mat = params.mGLTFMaterial;

                    LLGLDisable cull_face(gltf_mat && gltf_mat->mDoubleSided ? GL_CULL_FACE : 0);

                    if (gltf_mat && gltf_mat->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_BLEND)
                    {
                        target_shader = pbr_shader;
                        if (params.mAvatar != nullptr)
                        {
                            target_shader = target_shader->mRiggedVariant;
                        }

                        if (LLHLSLShader::sCurBoundShaderPtr != target_shader)
                        {
                            gPipeline.bindDeferredShaderFast(*target_shader);
                        }

                        params.mGLTFMaterial->bind(params.mTexture);
                    }
                    else
                    {
                        mat = LLPipeline::sRenderingHUDs ? nullptr : params.mMaterial;

                        if (params.mFullbright)
                        {
                            if (light_enabled || !initialized_lighting)
                            {
                                initialized_lighting = true;
                                target_shader = fullbright_shader;
                                light_enabled = false;
                            }
                        }
                        else if (!light_enabled || !initialized_lighting)
                        {
                            initialized_lighting = true;
                            target_shader = simple_shader;
                            light_enabled = true;
                        }

                        if (LLPipeline::sRenderingHUDs)
                        {
                            target_shader = fullbright_shader;
                        }
                        else if (mat)
                        {
                            U32 shader_mask = params.mShaderMask;
                            llassert(shader_mask < LLMaterial::SHADER_COUNT);
                            target_shader = &(gDeferredMaterialProgram[shader_mask]);
                        }
                        else if (!params.mFullbright)
                        {
                            target_shader = simple_shader;
                        }
                        else
                        {
                            target_shader = fullbright_shader;
                        }

                        if (params.mAvatar != nullptr)
                        {
                            llassert(target_shader->mRiggedVariant != nullptr);
                            target_shader = target_shader->mRiggedVariant;
                        }

                        if (LLHLSLShader::sCurBoundShaderPtr != target_shader)
                        {
                            gPipeline.bindDeferredShaderFast(*target_shader);

                            if (params.mFullbright)
                            {
                                S32 channel = target_shader->enableTexture(LLShaderMgr::EXPOSURE_MAP);
                                if (channel > -1)
                                {
                                    gDX.getTexUnit(channel)->bind(&gPipeline.mExposureMap);
                                }
                            }

                            // S24 (2026-09-09, perf pass): force a real set
                            // of all 3 uniforms below on this newly-bound
                            // shader - its own constant buffer hasn't seen
                            // these values yet, even if they happen to match
                            // whatever the PREVIOUSLY bound shader last had.
                            lastSpecColor.setVec(-1.f, -1.f, -1.f, -1.f);
                            lastEnvIntensity = -1.f;
                            lastBrightness = -1.f;
                        }

                        LLVector4 spec_color(1, 1, 1, 1);
                        F32 env_intensity = 0.0f;
                        F32 brightness = 1.0f;

                        if (mat)
                        {
                            spec_color = params.mSpecColor;
                            env_intensity = params.mEnvIntensity;
                            brightness = params.mFullbright ? 1.f : 0.f;
                        }

                        if (LLHLSLShader::sCurBoundShaderPtr)
                        {
                            if (spec_color != lastSpecColor)
                            {
                                lastSpecColor = spec_color;
                                LLHLSLShader::sCurBoundShaderPtr->uniform4f(LLShaderMgr::SPECULAR_COLOR, spec_color.mV[VRED], spec_color.mV[VGREEN], spec_color.mV[VBLUE], spec_color.mV[VALPHA]);
                            }
                            if (env_intensity != lastEnvIntensity)
                            {
                                lastEnvIntensity = env_intensity;
                                LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, env_intensity);
                            }
                            if (brightness != lastBrightness)
                            {
                                lastBrightness = brightness;
                                LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, brightness);
                            }
                        }
                    }

                    bool upload_ok = !params.mAvatar || LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, lastAvatarShader, skipLastSkin);

                    if (!upload_ok)
                    {
                        continue;
                    }

                    bool tex_setup = texSetup(&params, (mat != nullptr));

                    {
                        gDX.blendFunc((LLRender::eBlendFactor)params.mBlendFuncSrc, (LLRender::eBlendFactor)params.mBlendFuncDst, mAlphaSFactor, mAlphaDFactor);

                        bool reset_minimum_alpha = false;
                        if (!LLPipeline::sImpostorRender &&
                            params.mBlendFuncDst != LLRender::BF_SOURCE_ALPHA &&
                            params.mBlendFuncSrc != LLRender::BF_SOURCE_ALPHA)
                        {
                            LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(0.f);
                            reset_minimum_alpha = true;
                        }

                        params.mVertexBuffer->setBuffer();
                        params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);

                        if (reset_minimum_alpha)
                        {
                            LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(MINIMUM_ALPHA);
                        }
                    }

                    if (pool.getType() != LLDrawPool::POOL_ALPHA_PRE_WATER &&
                        params.mVertexBuffer->hasDataType(LLVertexBuffer::TYPE_EMISSIVE))
                    {
                        if (params.mAvatar != nullptr)
                        {
                            if (params.mGLTFMaterial.isNull())
                            {
                                rigged_emissives.push_back(&params);
                            }
                            else
                            {
                                pbr_rigged_emissives.push_back(&params);
                            }
                        }
                        else
                        {
                            if (params.mGLTFMaterial.isNull())
                            {
                                emissives.push_back(&params);
                            }
                            else
                            {
                                pbr_emissives.push_back(&params);
                            }
                        }
                    }

                    if (tex_setup)
                    {
                        gDX.getTexUnit(0)->activate();
                        gDX.matrixMode(LLRender::MM_TEXTURE);
                        gDX.loadIdentity();
                        gDX.matrixMode(LLRender::MM_MODELVIEW);
                    }
                }

                if (!depth_only)
                {
                    gPipeline.enableLightsDynamic();

                    // install glow-accumulating blend mode - see
                    // dxdrawpoolalpha.h class comment for the separate-
                    // alpha-factor documented gap this call runs into.
                    gDX.blendFunc(LLRender::BF_ZERO, LLRender::BF_ONE, LLRender::BF_ONE, LLRender::BF_ONE);

                    bool rebind = false;
                    LLHLSLShader* lastShader = LLHLSLShader::sCurBoundShaderPtr;
                    if (!emissives.empty())
                    {
                        light_enabled = true;
                        renderEmissives(emissives);
                        rebind = true;
                    }

                    if (!pbr_emissives.empty())
                    {
                        light_enabled = true;
                        renderPbrEmissives(pbr_emissives);
                        rebind = true;
                    }

                    if (!rigged_emissives.empty())
                    {
                        light_enabled = true;
                        renderRiggedEmissives(rigged_emissives);
                        rebind = true;
                    }

                    if (!pbr_rigged_emissives.empty())
                    {
                        light_enabled = true;
                        renderRiggedPbrEmissives(pbr_rigged_emissives);
                        rebind = true;
                    }

                    gDX.blendFunc(mColorSFactor, mColorDFactor, mAlphaSFactor, mAlphaDFactor);

                    if (lastShader && rebind)
                    {
                        lastShader->bind();
                    }
                }
            }
        }

        gDX.setSceneBlendType(LLRender::BT_ALPHA);

        LLVertexBuffer::unbind();

        if (!light_enabled)
        {
            gPipeline.enableLightsDynamic();
        }
    }

    // S24 (2026-08-09, task #170): now takes `rigged`, matching
    // lldrawpoolalpha.cpp's forwardRender(bool rigged) exactly.
    void forwardRender(LLDrawPoolAlpha& pool, bool rigged, AlphaAttachmentFilter filter = ATTACHMENT_ALL)
    {
        gPipeline.enableLightsDynamic();

        LLGLSPipelineAlpha gls_pipeline_alpha;
        gDX.setColorMask(true, true);

        bool write_depth = rigged
            || LLDrawPoolWater::sSkipScreenCopy
            || LLPipeline::sImpostorRenderAlphaDepthPass
            || pool.getType() == LLDrawPool::POOL_ALPHA_PRE_WATER;

        LLGLDepthTest depth(GL_TRUE, write_depth ? GL_TRUE : GL_FALSE);

        mColorSFactor = LLRender::BF_SOURCE_ALPHA;
        mColorDFactor = LLRender::BF_ONE_MINUS_SOURCE_ALPHA;
        mAlphaSFactor = LLRender::BF_ZERO;
        mAlphaDFactor = LLRender::BF_ONE_MINUS_SOURCE_ALPHA;
        gDX.blendFunc(mColorSFactor, mColorDFactor, mAlphaSFactor, mAlphaDFactor);

        if (rigged && pool.getType() == LLDrawPool::POOL_ALPHA_POST_WATER)
        { // draw GLTF scene to depth buffer before rigged alpha
            LL::GLTFSceneManager::instance().render(false, false);
            LL::GLTFSceneManager::instance().render(false, true);
            LL::GLTFSceneManager::instance().render(false, false, true);
            LL::GLTFSceneManager::instance().render(false, true, true);
        }

        renderAlpha(pool, pool.getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX | LLVertexBuffer::MAP_TANGENT | LLVertexBuffer::MAP_TEXCOORD1 | LLVertexBuffer::MAP_TEXCOORD2, false, rigged, filter);

        gDX.setColorMask(true, false);

        if (!rigged && (LLPipeline::sRenderingHUDs || pool.getType() == LLDrawPool::POOL_ALPHA_POST_WATER))
        {
            renderDebugAlpha(pool);
        }
    }
}

// static
void DXDrawPoolAlpha::renderPostDeferred(LLDrawPoolAlpha& pool, S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    if (LLPipeline::isWaterClip() && pool.getType() == LLDrawPool::POOL_ALPHA_PRE_WATER)
    {
        return;
    }

    F32 water_sign = 1.f;

    if (pool.getType() == LLDrawPool::POOL_ALPHA_PRE_WATER)
    {
        water_sign = -1.f;
    }

    if (LLPipeline::sUnderWaterRender)
    {
        water_sign *= -1.f;
    }

    llassert(LLPipeline::sRenderDeferred);

    emissive_shader = &gDeferredEmissiveProgram;
    prepare_alpha_shader(emissive_shader, false, water_sign);

    pbr_emissive_shader = &gPBRGlowProgram;
    prepare_alpha_shader(pbr_emissive_shader, false, water_sign);

    fullbright_shader =
        (LLPipeline::sImpostorRender) ? &gDeferredFullbrightAlphaMaskProgram :
        (LLPipeline::sRenderingHUDs) ? &gHUDFullbrightAlphaMaskAlphaProgram :
        &gDeferredFullbrightAlphaMaskAlphaProgram;
    prepare_alpha_shader(fullbright_shader, true, water_sign);

    simple_shader =
        (LLPipeline::sImpostorRender) ? &gDeferredAlphaImpostorProgram :
        (LLPipeline::sRenderingHUDs) ? &gHUDAlphaProgram :
        &gDeferredAlphaProgram;

    prepare_alpha_shader(simple_shader, true, water_sign);

    LLHLSLShader* materialShader = gDeferredMaterialProgram;
    for (int i = 0; i < LLMaterial::SHADER_COUNT * 2; ++i)
    {
        prepare_alpha_shader(&materialShader[i], true, water_sign);
    }

    pbr_shader =
        (LLPipeline::sRenderingHUDs) ? &gHUDPBRAlphaProgram :
        &gDeferredPBRAlphaProgram;

    prepare_alpha_shader(pbr_shader, true, water_sign);

    // S24 (2026-08-22, plan item D2 - investigated, reverted): a proposed
    // "structural fix" here (reset sCurBoundShaderPtr instead of the
    // explicit bindReflectionProbes call below) turned out to be built on a
    // mechanism that doesn't hold up: LLHLSLShader::unbind() (called a few
    // lines below, before the real draw loop) already sets
    // sCurBoundShaderPtr = nullptr under DX_RENDER (llhlslshader.cpp:1385),
    // so the dedup-cache-poisoning theory doesn't actually explain why the
    // explicit call below was needed. Since that explicit call is a live,
    // confirmed fix for a real bug ("glass floor shows zero reflections"),
    // and the true mechanism isn't yet understood, left AS-IS rather than
    // risk reintroducing a known bug on an unverified theory. Revisit only
    // with a live diagnostic proving the actual mechanism first.
    // S24 (2026-08-22, task #156 follow-up): pbralphaF.hlsl's real body
    // (class2/deferred, task #173) calls sampleReflectionProbes() -
    // probes_enabled/probe_intensity/etc (reflectionProbeF.hlsl's plain
    // top-level uniforms, NOT part of the b1 cbuffer) and the SSR
    // modelview_delta/inv_modelview_delta matrices are only ever uploaded
    // inside LLPipeline::bindReflectionProbes(shader) - and that call
    // targets THIS shader's own per-program uniform storage specifically
    // (same D3D11 per-shader-cbuffer limitation already fixed for the
    // legacy bump/shiny pool via this exact call, task #147/#184 -
    // dxdrawpoolbump.cpp's beginFullbrightShiny()). This alpha pool never
    // called it for gDeferredPBRAlphaProgram, so probes_enabled/the SSR
    // deltas were never set for PBR alpha-blend materials (glTF glass,
    // water-adjacent transparent surfaces) - real, confirmed root cause of
    // "glass floor shows zero reflections" while nearby opaque/bump-shiny
    // content (routed through bindDeferredShader()/dxdrawpoolbump.cpp,
    // both of which already call this) reflects correctly. HUD PBR alpha
    // has no deferredUtil.hlsl attachment (no reflectionProbeF.hlsl either,
    // see gHUDPBRAlphaProgram's own comment), so this is skipped for HUDs.
    if (!LLPipeline::sRenderingHUDs)
    {
        gPipeline.bindReflectionProbes(*pbr_shader);
    }

    LLHLSLShader::unbind();

    // S24 (2026-08-09, task #170): first pass, render rigged objects only
    // and render to depth buffer - matches lldrawpoolalpha.cpp's
    // renderPostDeferred() exactly. Was skipped entirely before the
    // vertex-layout wall (task #168) was fixed.
    if (LLPipeline::sRenderingHUDs)
    {
        // unchanged - HUDs never ran a rigged pass here anyway
        forwardRender(pool, false);
    }
    else if (pool.getType() == LLDrawPool::POOL_ALPHA_POST_WATER)
    {
        // S24 (2026-08-30, task #155, AYAstorm-derived alpha attachment-
        // order fix, LGPL v2.1, adopted per their public spec's explicit
        // "free adoption, no PR required" terms) - rigged content (hair)
        // drawing+depth-writing before non-rigged alpha behind it (windows,
        // lace, foliage) was causing that content to fail the depth test
        // and revert to raw skybox instead of blending. Splitting into 3
        // sub-passes (SIM non-rigged, then all rigged, then avatar-
        // attachment non-rigged) fixes that without the eyelash/eyebrow-
        // attachment regression AYAstorm's first, simpler 2-pass reorder
        // attempt hit (rigid non-skinned attachments have no mAvatar/
        // skinInfo, so they need their own late sub-pass via the broader
        // mAttachedToAvatar discriminator, not mAvatar). Does NOT fix the
        // original dome/glass-pane intra-mesh per-triangle sort bug (task
        // #155's WBOIT attempt, abandoned/reverted) - different bug.
        forwardRender(pool, false, ATTACHMENT_NONE); // SIM non-rigged first
        forwardRender(pool, true);                    // all rigged (depth-writing)
        forwardRender(pool, false, ATTACHMENT_ONLY);  // avatar-attachment non-rigged last
    }
    else
    {
        // PRE_WATER: unchanged original order (AYAstorm's own spec scopes
        // their fix to POST_WATER only, for water-fog integrity reasons)
        forwardRender(pool, true);
        forwardRender(pool, false);
    }

    if (!LLPipeline::sImpostorRender && LLPipeline::RenderDepthOfField && !gCubeSnapshot && !LLPipeline::sRenderingHUDs && pool.getType() == LLDrawPool::POOL_ALPHA_POST_WATER)
    {
        simple_shader = fullbright_shader = &gDeferredFullbrightAlphaMaskProgram;

        simple_shader->bind();
        simple_shader->setMinimumAlpha(0.33f);

        gDX.setColorMask(false, false);

        renderAlpha(pool, pool.getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX | LLVertexBuffer::MAP_TANGENT | LLVertexBuffer::MAP_TEXCOORD1 | LLVertexBuffer::MAP_TEXCOORD2,
            true, false);

        gDX.setColorMask(true, false);
    }
}
