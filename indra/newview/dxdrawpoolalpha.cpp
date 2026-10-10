/**
 * @file dxdrawpoolalpha.cpp
 * @brief Fresh DX11-native implementation of DXAlphaDrawPool's forward-alpha
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

#include "llviewershadermgr.h"
#include "llviewercontrol.h"
#include "llfasttimer.h"
#include "llrender.h"
#include "llface.h"
#include "llviewercamera.h"
#include "lllightconstants.h"
#include <algorithm>
#include <unordered_set>
#include <utility>
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewerregion.h"
#include "lldrawpoolwater.h"
#include "llspatialpartition.h"
#include "llenvironment.h"
#include "llviewertexture.h"
#include "llvoavatar.h"
#include "gltfscenemanager.h"
#include "DXStateCache.h" // KRLV_TOUCHPOINT: @camtextures stencil tagging, see krlv/README.md

extern bool gCubeSnapshot;

namespace
{
    // Mirrors DXAlphaDrawPool's private per-instance shader/blend-factor
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

    // Filters renderAlpha()'s draw loop by avatar-attachment status. Used by
    // renderPostDeferred()'s POST_WATER 3-pass split (see there) to avoid a
    // depth-order regression between rigged content and non-rigged alpha.
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
            shader->uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, DXDrawPoolAlpha::sWaterPlane.mV);
        }

        if (LLPipeline::sImpostorRender)
        {
            shader->setMinimumAlpha(MINIMUM_IMPOSTOR_ALPHA);
        }
        else
        {
            shader->setMinimumAlpha(MINIMUM_ALPHA);
        }

        if (shader->mRiggedVariant && shader->mRiggedVariant != shader)
        {
            prepare_alpha_shader(shader->mRiggedVariant, deferredEnvironment, water_sign);
        }
    }

    // Under DX_RENDER, indexed diffuse texture registers start at t5, not
    // t0, whenever the shader also attaches deferredUtil.hlsl (isDeferred ||
    // hasReflectionProbes - true for every alpha shader here), to avoid
    // colliding with its t0-t3 G-buffer/depth samplers. Must match
    // llshadermgr.cpp's kIndexedTexRegisterBase formula exactly.
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
                        // Must explicitly unbind here on null - this channel may still
                        // hold a stale bind from an earlier draw call (e.g. terrain's
                        // detail_0-3/alpha_ramp) in the same frame.
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

    void renderDebugAlpha(DXAlphaDrawPool& pool)
    {
        if (DXDrawPoolAlpha::sShowDebugAlpha && !gCubeSnapshot && !LLPipeline::sReflectionRender)
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

    // Called with rigged=false for static geometry (PASS_ALPHA /
    // beginAlphaGroups()) and rigged=true for rigged mesh
    // attachments/clothing/mesh bodies (PASS_ALPHA_RIGGED /
    // beginRiggedAlphaGroups()).
    // State that persists across the alpha batches of one renderAlpha() call.
struct AlphaDrawCarry
{
    bool initialized_lighting = false;
    bool light_enabled = true;
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    const LLHLSLShader* lastAvatarShader = nullptr;
    bool skipLastSkin = false;
    LLVector4 lastSpecColor = LLVector4(-1.f, -1.f, -1.f, -1.f);
    F32 lastEnvIntensity = -1.f;
    F32 lastBrightness = -1.f;
    std::vector<LLDrawInfo*> emissives;
    std::vector<LLDrawInfo*> rigged_emissives;
    std::vector<LLDrawInfo*> pbr_emissives;
    std::vector<LLDrawInfo*> pbr_rigged_emissives;
};

    // One face of a batch (sorted mode) or a whole batch (batch order, or far from the camera).
    struct AlphaEntry
    {
        LLDrawInfo*       batch = nullptr;
        const DXDrawFace* face = nullptr;
        F32               depth = 0.f;
        bool              disable_cull = false;
        bool              depth_write = false;  // near-opaque: writes depth so what is behind it is hidden
    };

    // Per-batch facts that the draw step needs after setup has run.
    struct PreparedBatch
    {
        bool tex_setup = false;
        bool cull_off = false;
    };

    // Radial distance from the eye to the nearest point of a face's box. Unlike a view-axis depth it
    // does not change when the camera turns, so the order cannot flip just from looking around.
    F32 faceDistance(const LLFace* face, const LLVector3& origin, F32 extent_factor)
    {
        LLVector4a box;
        box.setSub(face->mExtents[1], face->mExtents[0]);
        LLVector3 d = face->mCenterLocal - origin;
        F32 len = d.length();
        if (len < 1e-4f)
        {
            return 0.f;
        }
        LLVector3 dn = d * (1.f / len);
        F32 reach = box[0] * std::fabs(dn.mV[0]) + box[1] * std::fabs(dn.mV[1]) + box[2] * std::fabs(dn.mV[2]);
        return len - extent_factor * reach;
    }

    // Adds a batch's draw entries. A batch with any face near the camera gets one entry per face;
    // a batch that is entirely far away gets a single entry, which is the cheap path.
    void collectAlphaEntries(LLDrawInfo& params, bool disable_cull, const LLVector3& origin,
                             F32 near_distance, F32 extent_factor, F32 depth_write_alpha, S32 sort_mode, std::vector<AlphaEntry>& entries)
    {
        // A batch writes depth only if every face in it is at or above the threshold.
        F32 min_alpha = 1.f;
        for (const DXDrawFace& f : params.mDXFaces)
        {
            min_alpha = llmin(min_alpha, f.alpha);
        }
        bool batch_writes = !params.mDXFaces.empty() && min_alpha >= depth_write_alpha;

        if (sort_mode == 0 || params.mDXFaces.empty())
        {
            entries.push_back({ &params, nullptr, 0.f, disable_cull, batch_writes });
            return;
        }

        size_t first = entries.size();
        F32 nearest = 1e30f;
        F32 depth_sum = 0.f;
        U32 face_count = 0;
        for (const DXDrawFace& f : params.mDXFaces)
        {
            if (!f.face)
            {
                continue;
            }
            F32 depth = faceDistance(f.face, origin, extent_factor);
            nearest = llmin(nearest, depth);
            depth_sum += depth;
            ++face_count;
            entries.push_back({ &params, &f, depth, disable_cull, f.alpha >= depth_write_alpha });
        }

        if (face_count == 0)
        {
            entries.resize(first);
            entries.push_back({ &params, nullptr, 0.f, disable_cull, batch_writes });
        }
        else if (nearest > near_distance)
        {
            entries.resize(first);
            entries.push_back({ &params, nullptr, depth_sum / (F32)face_count, disable_cull, batch_writes });
        }
    }

    // Emissive batches are queued once per frame, in the order they are first seen.
    void queueAlphaEmissive(DXAlphaDrawPool& pool, LLDrawInfo& params, AlphaDrawCarry& c, std::unordered_set<LLDrawInfo*>& seen)
    {
        if (pool.getType() == LLDrawPool::POOL_ALPHA_PRE_WATER || !params.mVertexBuffer->hasDataType(LLVertexBuffer::TYPE_EMISSIVE))
        {
            return;
        }
        if (!seen.insert(&params).second)
        {
            return;
        }
        if (params.mAvatar != nullptr)
        {
            if (params.mGLTFMaterial.isNull()) c.rigged_emissives.push_back(&params);
            else                               c.pbr_rigged_emissives.push_back(&params);
        }
        else
        {
            if (params.mGLTFMaterial.isNull()) c.emissives.push_back(&params);
            else                               c.pbr_emissives.push_back(&params);
        }
    }

    // Binds the shader, material, uniforms and textures for one batch. Returns false if the batch is skipped.
    bool prepareAlphaBatch(DXAlphaDrawPool& pool, LLDrawInfo& params, AlphaDrawCarry& c, bool rigged, AlphaAttachmentFilter filter, PreparedBatch& prep)
    {
        (void)pool;

        if ((bool)params.mAvatar != rigged)
        {
            return false;
        }

        // See AlphaAttachmentFilter above; no-op when filter is
        // the default ATTACHMENT_ALL.
        if (filter == ATTACHMENT_NONE && params.mAttachedToAvatar)
        {
            return false;
        }
        if (filter == ATTACHMENT_ONLY && !params.mAttachedToAvatar)
        {
            return false;
        }

        LLRenderPass::applyModelMatrix(params);

        LLMaterial* mat = nullptr;
        LLGLTFMaterial* gltf_mat = params.mGLTFMaterial;


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
                if (c.light_enabled || !c.initialized_lighting)
                {
                    c.initialized_lighting = true;
                    target_shader = fullbright_shader;
                    c.light_enabled = false;
                }
            }
            else if (!c.light_enabled || !c.initialized_lighting)
            {
                c.initialized_lighting = true;
                target_shader = simple_shader;
                c.light_enabled = true;
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

                // Force a real re-upload of the 3 uniforms below - the
                // newly-bound shader's own constant buffer hasn't seen
                // them yet even if the values match the previous shader's.
                c.lastSpecColor.setVec(-1.f, -1.f, -1.f, -1.f);
                c.lastEnvIntensity = -1.f;
                c.lastBrightness = -1.f;
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
                if (spec_color != c.lastSpecColor)
                {
                    c.lastSpecColor = spec_color;
                    LLHLSLShader::sCurBoundShaderPtr->uniform4f(LLShaderMgr::SPECULAR_COLOR, spec_color.mV[VRED], spec_color.mV[VGREEN], spec_color.mV[VBLUE], spec_color.mV[VALPHA]);
                }
                if (env_intensity != c.lastEnvIntensity)
                {
                    c.lastEnvIntensity = env_intensity;
                    LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, env_intensity);
                }
                if (brightness != c.lastBrightness)
                {
                    c.lastBrightness = brightness;
                    LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, brightness);
                }
            }
        }

        bool upload_ok = !params.mAvatar || LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, c.lastAvatar, c.lastMeshId, c.lastAvatarShader, c.skipLastSkin);

        if (!upload_ok)
        {
            return false;
        }

        prep.cull_off = gltf_mat && gltf_mat->mDoubleSided;
        prep.tex_setup = texSetup(&params, (mat != nullptr));
        return true;
    }

    // Draws one entry with the state that prepareAlphaBatch() left bound.
    void drawAlphaEntry(LLDrawInfo& params, const AlphaEntry& e, const PreparedBatch& prep)
    {
        LLGLDisable cull(e.disable_cull || prep.cull_off ? GL_CULL_FACE : 0);

        gDX.blendFunc((LLRender::eBlendFactor)params.mBlendFuncSrc, (LLRender::eBlendFactor)params.mBlendFuncDst, mAlphaSFactor, mAlphaDFactor);

        bool reset_minimum_alpha = false;
        if (!LLPipeline::sImpostorRender &&
            params.mBlendFuncDst != LLRender::BF_SOURCE_ALPHA &&
            params.mBlendFuncSrc != LLRender::BF_SOURCE_ALPHA)
        {
            LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(0.f);
            reset_minimum_alpha = true;
        }

        // KRLV_TOUCHPOINT: @camtextures/@setcam_textures - same tagging as LLRenderPass::pushBatch()
        if (DXStateCache::sTagAttachmentStencilActive)
        {
            DXStateCache::tagAttachmentStencil(params.mAttachedToAvatar.notNull());
        }

        auto drawGeometry = [&]()
        {
            params.mVertexBuffer->setBuffer();
            if (e.face)
            {
                params.mVertexBuffer->drawRange(LLRender::TRIANGLES, e.face->vertStart, e.face->vertEnd, e.face->indexCount, e.face->indexOffset);
            }
            else
            {
                params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
            }
        };
        if (e.depth_write)
        {
            LLGLDepthTest depth_test(GL_TRUE, GL_TRUE);
            drawGeometry();
        }
        else
        {
            drawGeometry();
        }

        if (reset_minimum_alpha)
        {
            LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(MINIMUM_ALPHA);
        }

        if (prep.tex_setup)
        {
            gDX.getTexUnit(0)->activate();
            gDX.matrixMode(LLRender::MM_TEXTURE);
            gDX.loadIdentity();
            gDX.matrixMode(LLRender::MM_MODELVIEW);
        }
    }

    // Projected light on alpha-pool surfaces. The forward alpha shaders have no projector term, so each
    // selected spotlight gets one additive pass over the alpha entries, weighted by alpha and by its fade.
    //
    // Selection is sticky: a projector already selected keeps its slot while it is still in the nearby
    // list, so a light fading out holds its slot until it has faded, and lights do not swap in and out.
    // Free slots go to on-screen lights first, then to the nearest.
    void renderAlphaProjectors(const std::vector<AlphaEntry>& entries)
    {
        if (entries.empty() || !loadAlphaProjectorShader())
        {
            return;
        }

        static LLStaticHashedString sObjectAlpha("proj_object_alpha");
        static LLCachedControl<S32> max_projectors(gSavedSettings, "RenderAlphaProjectorCount", 2);
        static std::vector<LLDrawable*> held;

        LLHLSLShader& shader = gDeferredAlphaProjectorProgram;
        glm::mat4 modelview = get_current_modelview();
        LLViewerCamera* camera = LLViewerCamera::getInstance();
        const F32 deferred_light_falloff = 0.5f;
        const S32 limit = llclamp((S32)max_projectors, 1, 8);

        struct Candidate
        {
            LLDrawable* drawable;
            LLVOVolume* volume;
            F32         dist;
            F32         vis;
            bool        on_screen;
        };

        std::vector<Candidate> cands;
        for (const auto& light : gPipeline.getNearbyLights())
        {
            LLDrawable* drawablep = light.drawable;
            LLVOVolume* volume = drawablep ? drawablep->getVOVolume() : nullptr;
            if (!volume || !volume->isLightSpotlight())
            {
                continue;
            }

            // The same ramp the deferred light loop uses: fading in from 0, fading out back to 0.
            F32 vis = 1.f;
            if (light.fade < LIGHT_FADE_TIME)
            {
                vis = light.fade >= 0.f ? light.fade / LIGHT_FADE_TIME : 1.f + light.fade / LIGHT_FADE_TIME;
            }
            vis = llclamp(vis, 0.f, 1.f);

            LLVector3 agent = drawablep->getPositionAgent();
            LLVector4a center;
            center.load3(agent.mV);
            LLVector4a half;
            half.splat(volume->getLightRadius() * 1.5f);
            bool on_screen = camera->AABBInFrustumNoFarClip(center, half) != 0;

            cands.push_back({ drawablep, volume, light.dist, vis, on_screen });
        }

        // Held projectors that are still candidates keep their slot.
        std::vector<Candidate> chosen;
        for (LLDrawable* h : held)
        {
            for (const Candidate& c : cands)
            {
                if (c.drawable == h && (S32)chosen.size() < limit)
                {
                    chosen.push_back(c);
                    break;
                }
            }
        }

        // Free slots: on-screen first, then nearest.
        std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b)
        {
            if (a.on_screen != b.on_screen)
            {
                return a.on_screen;
            }
            return a.dist < b.dist;
        });
        for (const Candidate& c : cands)
        {
            if ((S32)chosen.size() >= limit)
            {
                break;
            }
            bool already = false;
            for (const Candidate& k : chosen)
            {
                if (k.drawable == c.drawable)
                {
                    already = true;
                    break;
                }
            }
            if (!already)
            {
                chosen.push_back(c);
            }
        }

        held.clear();
        for (const Candidate& c : chosen)
        {
            held.push_back(c.drawable);
        }

        for (const Candidate& c : chosen)
        {
            if (c.vis <= 0.f)
            {
                continue;
            }
            LLDrawable* drawablep = c.drawable;
            LLVOVolume* volume = c.volume;

            LLColor3 col = volume->getLightLinearColor() * c.vis;
            LLVector3 agent = drawablep->getPositionAgent();
            glm::vec4 v = modelview * glm::vec4(agent.mV[0], agent.mV[1], agent.mV[2], 1.f);
            F32 view_center[3] = { v.x, v.y, v.z };
            F32 size = volume->getLightRadius() * 1.5f;

            gPipeline.bindDeferredShaderFast(shader);
            gPipeline.setupSpotLight(shader, drawablep);
            shader.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, view_center);
            shader.uniform1f(LLShaderMgr::LIGHT_SIZE, size);
            shader.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, col.mV);
            shader.uniform1f(LLShaderMgr::LIGHT_FALLOFF, volume->getLightFalloff(deferred_light_falloff));

            shader.enableTexture(LLShaderMgr::DIFFUSE_MAP);
            LLGLDepthTest depth(GL_TRUE, GL_FALSE);
            LLGLEnable blend(GL_BLEND);
            gDX.blendFunc(LLRender::BF_ONE, LLRender::BF_ONE, LLRender::BF_ONE, LLRender::BF_ONE);

            for (const AlphaEntry& e : entries)
            {
                LLDrawInfo* batch = e.batch;
                // Avatars and glTF materials keep their own paths for now.
                if (batch->mAvatar != nullptr || batch->mGLTFMaterial.notNull())
                {
                    continue;
                }

                F32 alpha = 1.f;
                if (e.face)
                {
                    alpha = e.face->alpha;
                }
                else
                {
                    for (const DXDrawFace& f : batch->mDXFaces)
                    {
                        alpha = llmin(alpha, f.alpha);
                    }
                }

                LLRenderPass::applyModelMatrix(*batch);
                shader.uniform1f(sObjectAlpha, alpha);
                if (batch->mTexture.notNull())
                {
                    shader.bindTexture(LLShaderMgr::DIFFUSE_MAP, batch->mTexture);
                }

                batch->mVertexBuffer->setBuffer();
                if (e.face)
                {
                    batch->mVertexBuffer->drawRange(LLRender::TRIANGLES, e.face->vertStart, e.face->vertEnd, e.face->indexCount, e.face->indexOffset);
                }
                else
                {
                    batch->mVertexBuffer->drawRange(LLRender::TRIANGLES, batch->mStart, batch->mEnd, batch->mCount, batch->mOffset);
                }
            }

            shader.disableTexture(LLShaderMgr::DIFFUSE_MAP);
            gPipeline.unbindDeferredShader(shader);
        }

        gDX.setSceneBlendType(LLRender::BT_ALPHA);
    }

    // Draws every alpha batch of one pass in one back-to-front order across all visible groups.
    void renderAlpha(DXAlphaDrawPool& pool, U32 mask, bool depth_only, bool rigged, AlphaAttachmentFilter filter = ATTACHMENT_ALL)
    {
        AlphaDrawCarry c;

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

        static LLCachedControl<S32> sort_mode(gSavedSettings, "RenderAlphaSortMode", 1);
        static LLCachedControl<F32> near_distance(gSavedSettings, "RenderAlphaFaceSortDistance", 24.f);
        static LLCachedControl<F32> extent_factor(gSavedSettings, "RenderAlphaSortExtentFactor", 0.5f);
        static LLCachedControl<F32> depth_write_alpha(gSavedSettings, "RenderAlphaDepthWriteThreshold", 0.98f);
        const S32 effective_sort_mode = rigged ? 0 : sort_mode;

        LLViewerCamera* camera = LLViewerCamera::getInstance();
        LLVector3 origin = camera->getOrigin();

        // Gather. Groups are filtered as before; batches keep their group order when sorting is off.
        static std::vector<AlphaEntry> entries;
        static std::unordered_set<LLDrawInfo*> seen_emissive;
        entries.clear();
        seen_emissive.clear();

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

                bool is_particle_or_hud_particle = group->getSpatialPartition()->mPartitionType == LLViewerRegion::PARTITION_PARTICLE
                                                          || group->getSpatialPartition()->mPartitionType == LLViewerRegion::PARTITION_HUD_PARTICLE;
                bool disable_cull = is_particle_or_hud_particle;

                LLSpatialGroup::drawmap_elem_t& draw_info = rigged ? group->mDrawMap[LLRenderPass::PASS_ALPHA_RIGGED] : group->mDrawMap[LLRenderPass::PASS_ALPHA];

                for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)
                {
                    LLDrawInfo& params = **k;
                    if ((bool)params.mAvatar != rigged)
                    {
                        continue;
                    }
                    if (filter == ATTACHMENT_NONE && params.mAttachedToAvatar)
                    {
                        continue;
                    }
                    if (filter == ATTACHMENT_ONLY && !params.mAttachedToAvatar)
                    {
                        continue;
                    }

                    if (!depth_only)
                    {
                        queueAlphaEmissive(pool, params, c, seen_emissive);
                    }
                    collectAlphaEntries(params, disable_cull, origin, near_distance, extent_factor, depth_write_alpha, effective_sort_mode, entries);
                }
            }
        }

        if (effective_sort_mode != 0)
        {
            // Farthest first. stable_sort keeps gather order for exact ties, so the order repeats frame to frame.
            std::stable_sort(entries.begin(), entries.end(), [](const AlphaEntry& a, const AlphaEntry& b)
            {
                return a.depth > b.depth;
            });
        }

        // Draw. Setup is redone when the batch changes; consecutive entries of one batch reuse it.
        LLDrawInfo* last_batch = nullptr;
        bool skip_batch = false;
        PreparedBatch prep;
        for (const AlphaEntry& e : entries)
        {
            if (e.batch != last_batch)
            {
                last_batch = e.batch;
                skip_batch = !prepareAlphaBatch(pool, *e.batch, c, rigged, filter, prep);
            }
            if (!skip_batch)
            {
                drawAlphaEntry(*e.batch, e, prep);
            }
        }

        static LLCachedControl<bool> alpha_projectors(gSavedSettings, "RenderAlphaProjectors", false);
        if (alpha_projectors && !depth_only && !rigged)
        {
            renderAlphaProjectors(entries);
        }

        if (!depth_only)
        {
            gPipeline.enableLightsDynamic();

            // install glow-accumulating blend mode - see dxdrawpoolalpha.h class comment for the
            // separate-alpha-factor documented gap this call runs into.
            gDX.blendFunc(LLRender::BF_ZERO, LLRender::BF_ONE, LLRender::BF_ONE, LLRender::BF_ONE);

            bool rebind = false;
            LLHLSLShader* lastShader = LLHLSLShader::sCurBoundShaderPtr;
            if (!c.emissives.empty())
            {
                c.light_enabled = true;
                renderEmissives(c.emissives);
                rebind = true;
            }
            if (!c.pbr_emissives.empty())
            {
                c.light_enabled = true;
                renderPbrEmissives(c.pbr_emissives);
                rebind = true;
            }
            if (!c.rigged_emissives.empty())
            {
                c.light_enabled = true;
                renderRiggedEmissives(c.rigged_emissives);
                rebind = true;
            }
            if (!c.pbr_rigged_emissives.empty())
            {
                c.light_enabled = true;
                renderRiggedPbrEmissives(c.pbr_rigged_emissives);
                rebind = true;
            }

            gDX.blendFunc(mColorSFactor, mColorDFactor, mAlphaSFactor, mAlphaDFactor);

            if (lastShader && rebind)
            {
                lastShader->bind();
            }
        }

        gDX.setSceneBlendType(LLRender::BT_ALPHA);

        LLVertexBuffer::unbind();

        if (!c.light_enabled)
        {
            gPipeline.enableLightsDynamic();
        }
    }

    void forwardRender(DXAlphaDrawPool& pool, bool rigged, AlphaAttachmentFilter filter = ATTACHMENT_ALL)
    {
        gPipeline.enableLightsDynamic();

        LLGLSPipelineAlpha gls_pipeline_alpha;
        gDX.setColorWriteMask(true, true);

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

        gDX.setColorWriteMask(true, false);

        if (!rigged && (LLPipeline::sRenderingHUDs || pool.getType() == LLDrawPool::POOL_ALPHA_POST_WATER))
        {
            renderDebugAlpha(pool);
        }
    }
}

// static
bool DXDrawPoolAlpha::sShowDebugAlpha = false;
LLVector4 DXDrawPoolAlpha::sWaterPlane;

void DXDrawPoolAlpha::renderPostDeferred(DXAlphaDrawPool& pool, S32 pass)
{

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

    // Do not remove as "redundant" - despite sCurBoundShaderPtr already
    // being reset to nullptr in unbind() below, this explicit call is a
    // confirmed fix for reflections vanishing on PBR alpha-blend materials;
    // the exact mechanism isn't understood.
    //
    // pbralphaF.hlsl's sampleReflectionProbes() uniforms (probes_enabled,
    // probe_intensity, SSR modelview_delta matrices) live in this shader's
    // own per-program cbuffer (D3D11's per-shader-cbuffer limit - same
    // reason dxdrawpoolbump.cpp's beginFullbrightShiny() needs this call for
    // bump/shiny). Skipped for HUDs: HUD PBR alpha has no
    // reflectionProbeF.hlsl attachment.
    if (!LLPipeline::sRenderingHUDs)
    {
        gPipeline.bindReflectionProbes(*pbr_shader);
    }

    LLHLSLShader::unbind();

    if (LLPipeline::sRenderingHUDs)
    {
        // unchanged - HUDs never ran a rigged pass here anyway
        forwardRender(pool, false);
    }
    else if (pool.getType() == LLDrawPool::POOL_ALPHA_POST_WATER)
    {
        // Rigged content (hair) writes depth first; non-rigged alpha behind it
        // (windows, lace, foliage) must draw before that or fail the depth
        // test and revert to skybox. But avatar attachments (eyelashes,
        // eyebrows) must draw AFTER the rigged pass or get over-blended into
        // invisibility. Hence 3 sub-passes, filtered by mAttachedToAvatar
        // (not mAvatar - rigid non-skinned attachments have neither).
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

        gDX.setColorWriteMask(false, false);

        renderAlpha(pool, pool.getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX | LLVertexBuffer::MAP_TANGENT | LLVertexBuffer::MAP_TEXCOORD1 | LLVertexBuffer::MAP_TEXCOORD2,
            true, false);

        gDX.setColorWriteMask(true, false);
    }
}

DXAlphaDrawPool::DXAlphaDrawPool(U32 type) :
        LLRenderPass(type)
{
}

DXAlphaDrawPool::~DXAlphaDrawPool()
{
}

void DXAlphaDrawPool::prerender()
{
    mShaderLevel = LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

S32 DXAlphaDrawPool::getNumPostDeferredPasses()
{
    return 1;
}

void DXAlphaDrawPool::renderPostDeferred(S32 pass)
{
    DXDrawPoolAlpha::renderPostDeferred(*this, pass);
}
