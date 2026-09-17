/**
 * @file dxdrawpoolbump.cpp
 * @brief Fresh DX11-native implementation of LLDrawPoolBump's deferred bump
 * and fullbright-shiny/emboss-bump render paths.
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

#include "dxdrawpoolbump.h"

#include "lldrawpoolbump.h"
#include "llrender.h"
#include "DXCubeMap.h"
#include "llsky.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llspatialpartition.h"
#include "DXDevice.h"
#include "DXStateCache.h"

namespace
{
    // Mirrors lldrawpoolbump.cpp's own file-static shader/channel state -
    // separate copies, internal linkage, no collision with the GL file's.
    LLHLSLShader* shader = nullptr;
    S32 cube_channel = -1;
    S32 diffuse_channel = -1;

    void beginFullbrightShiny(LLDrawPoolBump& pool, bool rigged)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_SHINY);

        shader = &gDeferredFullbrightShinyProgram;
        if (LLPipeline::sRenderingHUDs)
        {
            shader = &gHUDFullbrightShinyProgram;
        }

        if (rigged)
        {
            llassert(shader->mRiggedVariant);
            shader = shader->mRiggedVariant;
        }

        // bind exposure map so fullbright shader can cancel out exposure
        S32 channel = shader->enableTexture(LLShaderMgr::EXPOSURE_MAP);
        if (channel > -1)
        {
            gDX.getTexUnit(channel)->bind(&gPipeline.mExposureMap);
        }

        DXCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : nullptr;

        // use_legacy_env_map mirrors LLPipeline::bindDeferredShader()'s own
        // legacy-vs-modern env-map override (pipeline.cpp) - this pool has its
        // own separate copy of that branching since it doesn't route through
        // bindDeferredShader() at all, so keep the two in sync.
        bool use_legacy_env_map = !LLPipeline::sReflectionProbesEnabled;

        if (cube_map && use_legacy_env_map)
        {
            gDX.getTexUnit(1)->disable();
            cube_channel = shader->enableTexture(LLViewerShaderMgr::ENVIRONMENT_MAP, LLTexUnit::TT_CUBE_MAP);
            cube_map->enableTexture(cube_channel);
            diffuse_channel = shader->enableTexture(LLViewerShaderMgr::DIFFUSE_MAP);

            gDX.getTexUnit(cube_channel)->bind(cube_map);
            gDX.getTexUnit(0)->activate();
        }

        {
            LLMatrix4 mat;
            mat.initRows(LLVector4(gGLModelView + 0),
                         LLVector4(gGLModelView + 4),
                         LLVector4(gGLModelView + 8),
                         LLVector4(gGLModelView + 12));
            shader->bind();

            LLVector3 vec = LLVector3(gShinyOrigin) * mat;
            LLVector4 vec4(vec, gShinyOrigin.mV[3]);
            shader->uniform4fv(LLViewerShaderMgr::SHINY_ORIGIN, 1, vec4.mV);

            if (use_legacy_env_map)
            {
                gPipeline.setEnvMat(*shader);
            }
            else
            {
                gPipeline.bindReflectionProbes(*shader);
            }
        }

        if (pool.mShaderLevel > 1)
        { //indexed texture rendering, channel 0 is always diffuse
            diffuse_channel = 0;
        }
    }

    void renderFullbrightShiny(LLDrawPoolBump& pool, bool rigged)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_SHINY);

        LLGLEnable blend_enable(GL_BLEND);

        // S24: gDeferredFullbrightShinyProgram sets mFeatures.mIndexedTextureChannels
        // unconditionally (llviewershadermgr.cpp), not gated on mShaderLevel - the shader
        // always compiles with HAS_DIFFUSE_LOOKUP and always expects a texture batch's
        // mTextureList bound across tex0..tex3. Gating batch_textures on mShaderLevel>1
        // here disagreed with that: at mShaderLevel<=1, real multi-texture LLDrawInfo
        // batches (built by the geometry side regardless of shader level) got pushed with
        // batch_textures=false, so pushBatch() only ever bound ONE of the batch's several
        // real textures (params.mTexture) to tex0/t5, leaving tex1-tex3 unbound - any face
        // whose vary_texture_index picked one of those read the D3D11 null-SRV default
        // (white/grey), while whichever face happened to land on index 0 rendered fine.
        // Matches LLDrawPoolSimple::renderDeferred()'s own unconditional
        // pushBatches(PASS_SIMPLE, true, true) - same shader capability, same fix.
        if (rigged)
        {
            pool.pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_SHINY_RIGGED, true, true);
        }
        else
        {
            pool.pushBatches(LLRenderPass::PASS_FULLBRIGHT_SHINY, true, true);
        }
    }

    void endFullbrightShiny()
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_SHINY);

        // Mirrors beginFullbrightShiny()'s legacy/modern branch: unbindReflectionProbes()
        // is intentionally skipped on the legacy path, since begin() called
        // setEnvMat() there instead of bindReflectionProbes().
        DXCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : nullptr;
        bool use_legacy_env_map = !LLPipeline::sReflectionProbesEnabled;
        if (cube_map && use_legacy_env_map)
        {
            cube_map->disable();
            shader->unbind();
        }
        else if (cube_map && shader->mFeatures.hasReflectionProbes)
        {
            gPipeline.unbindReflectionProbes(*shader);
        }

        diffuse_channel = -1;
        cube_channel = 0;
    }

    void beginBump(LLDrawPoolBump& pool, bool rigged)
    {
        (void)pool; // unused - kept for signature symmetry with the other begin*/render* helpers
        LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);

        shader = &gObjectBumpProgram;

        if (rigged)
        {
            llassert(shader->mRiggedVariant);
            shader = shader->mRiggedVariant;
        }

        shader->bind();

        gDX.setSceneBlendType(LLRender::BT_MULT_X2);
    }

    void renderBump(LLDrawPoolBump& pool)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);
        LLGLDepthTest gls_depth(GL_TRUE, GL_FALSE, GL_LEQUAL);
        LLGLEnable blend(GL_BLEND);
        gDX.diffuseColor4f(1, 1, 1, 1);

        // Biases depth (-1,-1), matching GL's glPolygonOffset(-1,-1): this
        // emboss-bump pass draws a second, MULTIPLY-blended layer at the same
        // depth as the base surface it decorates, and without the bias the two
        // passes z-fight on curved/grazing-angle geometry (thin foliage
        // especially). Reads current cull/scissor/depth-clamp so it doesn't
        // clobber other active state, matching other applyDXState() rasterizer cases.
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ID3D11RasterizerState* biased_rs = DXStateCache::getRasterizerState(
            DXState::isEnabled(GL_CULL_FACE), DXState::isEnabled(GL_SCISSOR_TEST), DXState::isEnabled(GL_DEPTH_CLAMP), -1.0f, -1.0f);
        ctx->RSSetState(biased_rs);

        pool.pushBumpBatches(LLRenderPass::PASS_POST_BUMP);

        // Restore the non-biased state so nothing after this pass inherits
        // the bias unexpectedly.
        ID3D11RasterizerState* normal_rs = DXStateCache::getRasterizerState(
            DXState::isEnabled(GL_CULL_FACE), DXState::isEnabled(GL_SCISSOR_TEST), DXState::isEnabled(GL_DEPTH_CLAMP), 0.f, 0.f);
        ctx->RSSetState(normal_rs);
    }

    // Rigged counterpart of renderBump() above. LLDrawPoolBump::mRigged (which
    // pushBumpBatches() checks internally) is private, so this DX pool can't
    // just call pushBumpBatches(PASS_POST_BUMP) a second time for rigged
    // behavior - the loop is reimplemented here using only public members
    // (bindBumpMap()/uploadMatrixPalette()/pushBumpBatch()).
    void renderBumpRigged(LLDrawPoolBump& pool)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);
        LLGLDepthTest gls_depth(GL_TRUE, GL_FALSE, GL_LEQUAL);
        LLGLEnable blend(GL_BLEND);
        gDX.diffuseColor4f(1, 1, 1, 1);

        // Same depth-bias fix as renderBump() above - rigged foliage/attachments
        // need it just as much as static mesh does.
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ID3D11RasterizerState* biased_rs = DXStateCache::getRasterizerState(
            DXState::isEnabled(GL_CULL_FACE), DXState::isEnabled(GL_SCISSOR_TEST), DXState::isEnabled(GL_DEPTH_CLAMP), -1.0f, -1.0f);
        ctx->RSSetState(biased_rs);

        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;

        LLCullResult::drawinfo_iterator begin = gPipeline.beginRenderMap(LLRenderPass::PASS_POST_BUMP_RIGGED);
        LLCullResult::drawinfo_iterator end = gPipeline.endRenderMap(LLRenderPass::PASS_POST_BUMP_RIGGED);

        for (LLCullResult::drawinfo_iterator i = begin; i != end; ++i)
        {
            LLDrawInfo& params = **i;

            if (LLDrawPoolBump::bindBumpMap(params))
            {
                if (LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                {
                    pool.pushBumpBatch(params, false);
                }
            }
        }

        ID3D11RasterizerState* normal_rs = DXStateCache::getRasterizerState(
            DXState::isEnabled(GL_CULL_FACE), DXState::isEnabled(GL_SCISSOR_TEST), DXState::isEnabled(GL_DEPTH_CLAMP), 0.f, 0.f);
        ctx->RSSetState(normal_rs);
    }

    void endBump()
    {
        LLHLSLShader::unbind();
        gDX.setSceneBlendType(LLRender::BT_ALPHA);
    }
}

// static
void DXDrawPoolBump::renderDeferred(LLDrawPoolBump& pool, S32 pass)
{
    (void)pool; // unused - this body needs no per-instance state
    (void)pass; // unused - only ever called with a single (non-rigged) pass here
    LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);

    // Loops static (pass_i==0) then rigged (pass_i==1), matching
    // lldrawpoolbump.cpp's renderDeferred().
    for (int pass_i = 0; pass_i < 2; ++pass_i)
    {
        bool rigged = (pass_i == 1);
        gDeferredBumpProgram.bind(rigged);

        // bump_channel is resolved via LLHLSLShader::enableTexture()'s shader-reflection
        // lookup (bumpF.hlsl declares bumpMap at register t1); LLDrawPoolBump::bindBumpMap()
        // (lldrawpoolbump.cpp) is backend-agnostic and does the actual per-drawinfo texture
        // selection from bump_code. diffuse_channel stays hardcoded to unit 0 below -
        // bumpF.hlsl samples diffuse at t0.
        S32 bump_channel = LLHLSLShader::sCurBoundShaderPtr->enableTexture(LLViewerShaderMgr::BUMP_MAP);
        if (bump_channel > -1)
        {
            gDX.getTexUnit(bump_channel)->unbind(LLTexUnit::TT_TEXTURE);
        }

        const U32 type = rigged ? LLRenderPass::PASS_BUMP_RIGGED : LLRenderPass::PASS_BUMP;
        LLCullResult::drawinfo_iterator begin = gPipeline.beginRenderMap(type);
        LLCullResult::drawinfo_iterator end = gPipeline.endRenderMap(type);

        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;
        // setMinimumAlpha() (llhlslshader.cpp) does a gDX.flush() plus a uniform
        // upload, so it's gated behind a last-value comparison rather than called
        // per draw item. -1.f is outside mAlphaMaskCutoff's valid [0,1] range so
        // the first item always uploads once.
        F32 lastAlphaMaskCutoff = -1.f;

        for (LLCullResult::drawinfo_iterator i = begin; i != end; )
        {
            LLDrawInfo& params = **i;
            LLCullResult::increment_iterator(i, end);

            if (lastAlphaMaskCutoff != params.mAlphaMaskCutoff)
            {
                lastAlphaMaskCutoff = params.mAlphaMaskCutoff;
                LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(lastAlphaMaskCutoff);
            }
            if (bump_channel > -1)
            {
                LLDrawPoolBump::bindBumpMap(params, bump_channel);
            }

            if (rigged)
            {
                if (!LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                {
                    continue;
                }
            }

            LLRenderPass::applyModelMatrix(params);

            if (params.mTexture.notNull())
            {
                gDX.getTexUnit(0)->bindFast(params.mTexture);
            }
            else
            {
                gDX.getTexUnit(0)->unbindFast(LLTexUnit::TT_TEXTURE);
            }

            params.mVertexBuffer->setBuffer();
            params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
        }

        if (bump_channel > -1)
        {
            LLHLSLShader::sCurBoundShaderPtr->disableTexture(LLViewerShaderMgr::BUMP_MAP);
        }
        LLHLSLShader::sCurBoundShaderPtr->unbind();
        gDX.getTexUnit(0)->activate();
    }
}

// static
void DXDrawPoolBump::renderPostDeferred(LLDrawPoolBump& pool, S32 pass)
{
    (void)pass; // unused - only ever called with a single (non-rigged) pass here

    // Runs 2 passes (static + rigged) unless rendering HUDs, matching
    // lldrawpoolbump.cpp's renderPostDeferred().
    S32 num_passes = LLPipeline::sRenderingHUDs ? 1 : 2;

    for (S32 i = 0; i < num_passes; ++i)
    {
        bool rigged = (i == 1);

        beginFullbrightShiny(pool, rigged);
        renderFullbrightShiny(pool, rigged);
        endFullbrightShiny();

        beginBump(pool, rigged);
        if (rigged)
        {
            renderBumpRigged(pool);
        }
        else
        {
            renderBump(pool);
        }
        endBump();
    }
}
