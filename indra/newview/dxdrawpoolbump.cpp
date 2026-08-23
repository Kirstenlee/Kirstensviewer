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
#include "llcubemap.h"
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
    LLGLSLShader* shader = nullptr;
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

        // S24 (2026-08-09, task #170): was never selected - rigged batches
        // were skipped entirely until task #168 fixed DXVertexLayout's
        // MAP_WEIGHT4 rejection.
        if (rigged)
        {
            llassert(shader->mRiggedVariant);
            shader = shader->mRiggedVariant;
        }

        // bind exposure map so fullbright shader can cancel out exposure
        S32 channel = shader->enableTexture(LLShaderMgr::EXPOSURE_MAP);
        if (channel > -1)
        {
            gGL.getTexUnit(channel)->bind(&gPipeline.mExposureMap);
        }

        LLCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : nullptr;

        // S24 (2026-08-09, task #123 follow-up): mirrors LLPipeline::
        // bindDeferredShader()'s own "use_legacy_env_map" override
        // (pipeline.cpp, task #113, 2026-08-06) - this pool has its OWN
        // separate copy of the legacy-vs-modern env-map branching (it
        // doesn't route through bindDeferredShader() at all), and that
        // copy was never updated to match. Real, confirmed consequence:
        // LLPipeline::bindReflectionProbes() (the "modern" branch below)
        // never binds ENVIRONMENT_MAP at all - only the array-based
        // REFLECTION_PROBES/IRRADIANCE_PROBES, which stay null forever
        // under DX_RENDER since the real capture pipeline never runs (see
        // llreflectionmapmanager.cpp's own DX_RENDER gate, task #147).
        // With sReflectionProbesEnabled true (the default), this pool was
        // taking the "modern" branch, calling bindReflectionProbes()
        // which does nothing useful, leaving "environmentMap" (what the
        // current simplified reflectionProbeF.hlsl actually samples)
        // permanently unbound - shiny/bump materials read whatever
        // texture an unrelated earlier draw call left in that slot. This
        // is what was showing through water's own (legitimate, working)
        // screen-space refraction as a "fishbowl"-looking reflection -
        // water's own radiance term was already fixed/zeroed (task #147),
        // but water was honestly reflecting already-broken nearby shiny
        // objects.
        //
        // S24 (2026-08-10, task #147/#184, SUPERSEDED): the real capture
        // pipeline this comment describes as "never runs" now does - see
        // pipeline.cpp's own bindDeferredShader() override (same fix,
        // fuller writeup there). reflectionProbeF.hlsl already declares
        // and samples the real TextureCubeArray reflectionProbes/
        // irradianceProbes registers (t16/t17) - bindReflectionProbes()
        // (the "modern" branch below) is what feeds those, so matching
        // GL's condition here now gets bump/shiny materials real,
        // correctly-oriented probe data instead of a legacy cubemap whose
        // own producer (llvosky.cpp) stopped updating it the moment
        // sReflectionProbesEnabled went true.
        bool use_legacy_env_map = !LLPipeline::sReflectionProbesEnabled;

        if (cube_map && use_legacy_env_map)
        {
            gGL.getTexUnit(1)->disable();
            cube_channel = shader->enableTexture(LLViewerShaderMgr::ENVIRONMENT_MAP, LLTexUnit::TT_CUBE_MAP);
            cube_map->enableTexture(cube_channel);
            diffuse_channel = shader->enableTexture(LLViewerShaderMgr::DIFFUSE_MAP);

            gGL.getTexUnit(cube_channel)->bind(cube_map);
            gGL.getTexUnit(0)->activate();
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

        // S24 (2026-08-09, task #170): render rigged - was skipped until
        // task #168 fixed DXVertexLayout's MAP_WEIGHT4 rejection.
        if (rigged)
        {
            if (pool.mShaderLevel > 1)
            {
                pool.pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_SHINY_RIGGED, true, true);
            }
            else
            {
                pool.pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_SHINY_RIGGED);
            }
        }
        else if (pool.mShaderLevel > 1)
        {
            pool.pushBatches(LLRenderPass::PASS_FULLBRIGHT_SHINY, true, true);
        }
        else
        {
            pool.pushBatches(LLRenderPass::PASS_FULLBRIGHT_SHINY);
        }
    }

    void endFullbrightShiny()
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_SHINY);

        // S24 (2026-08-09, task #123 follow-up, SUPERSEDED 2026-08-10 task
        // #147/#184): mirrors beginFullbrightShiny()'s own override - see
        // its fuller writeup there. unbindReflectionProbes() is
        // intentionally NOT called when using the legacy path, matching
        // begin*()'s choice to call setEnvMat() instead of
        // bindReflectionProbes() there.
        LLCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : nullptr;
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

        // S24 (2026-08-09, task #170): was never selected - rigged batches
        // were skipped entirely until task #168 fixed DXVertexLayout's
        // MAP_WEIGHT4 rejection.
        if (rigged)
        {
            llassert(shader->mRiggedVariant);
            shader = shader->mRiggedVariant;
        }

        shader->bind();

        gGL.setSceneBlendType(LLRender::BT_MULT_X2);
    }

    void renderBump(LLDrawPoolBump& pool)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);
        LLGLDepthTest gls_depth(GL_TRUE, GL_FALSE, GL_LEQUAL);
        LLGLEnable blend(GL_BLEND);
        gGL.diffuseColor4f(1, 1, 1, 1);

        // S24 (2026-08-19, degenerate-triangle foliage investigation): real
        // fix, replacing the "no DX11 runtime equivalent" gap this comment
        // used to describe - see DXStateCache::getRasterizerState()'s
        // depth_bias_enabled param for the full writeup. This emboss-bump
        // pass draws a second, MULTIPLY-blended layer at the SAME depth as
        // the base surface it's decorating; without a bias pushing it
        // slightly toward the camera (matching GL's glPolygonOffset(-1,-1)),
        // the two passes z-fight per-pixel/per-triangle on any curved or
        // grazing-angle geometry (thin mesh foliage leaves being the
        // reported case) - visually a blotchy, triangulated darkening
        // pattern, not a smooth bump effect. Read current cull/scissor/
        // depth-clamp so this doesn't clobber whatever's already active,
        // matching every other applyDXState() rasterizer case's pattern.
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ID3D11RasterizerState* biased_rs = DXStateCache::getRasterizerState(
            LLGLState::isEnabled(GL_CULL_FACE), LLGLState::isEnabled(GL_SCISSOR_TEST), LLGLState::isEnabled(GL_DEPTH_CLAMP), true);
        ctx->RSSetState(biased_rs);

        pool.pushBumpBatches(LLRenderPass::PASS_POST_BUMP);

        // Restore the non-biased state so nothing after this pass inherits
        // the bias unexpectedly.
        ID3D11RasterizerState* normal_rs = DXStateCache::getRasterizerState(
            LLGLState::isEnabled(GL_CULL_FACE), LLGLState::isEnabled(GL_SCISSOR_TEST), LLGLState::isEnabled(GL_DEPTH_CLAMP), false);
        ctx->RSSetState(normal_rs);
    }

    // S24 (2026-08-09, task #170): rigged counterpart of renderBump() above.
    // Can't just call pool.pushBumpBatches(PASS_POST_BUMP) a second time
    // expecting rigged behavior - LLDrawPoolBump::mRigged (which that real
    // member function checks internally) is private and this DX pool has no
    // access to it, so the rigged loop is reimplemented here directly,
    // mirroring pushBumpBatches()'s own rigged branch (lldrawpoolbump.cpp)
    // using only public members (bindBumpMap()/uploadMatrixPalette()/
    // pushBumpBatch()).
    void renderBumpRigged(LLDrawPoolBump& pool)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);
        LLGLDepthTest gls_depth(GL_TRUE, GL_FALSE, GL_LEQUAL);
        LLGLEnable blend(GL_BLEND);
        gGL.diffuseColor4f(1, 1, 1, 1);

        // S24 (2026-08-19): same real depth-bias fix as renderBump() above -
        // see its comment for the full writeup. Rigged foliage/attachments
        // need this exactly as much as static mesh does.
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ID3D11RasterizerState* biased_rs = DXStateCache::getRasterizerState(
            LLGLState::isEnabled(GL_CULL_FACE), LLGLState::isEnabled(GL_SCISSOR_TEST), LLGLState::isEnabled(GL_DEPTH_CLAMP), true);
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
            LLGLState::isEnabled(GL_CULL_FACE), LLGLState::isEnabled(GL_SCISSOR_TEST), LLGLState::isEnabled(GL_DEPTH_CLAMP), false);
        ctx->RSSetState(normal_rs);
    }

    void endBump()
    {
        LLGLSLShader::unbind();
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
    }
}

// static
void DXDrawPoolBump::renderDeferred(LLDrawPoolBump& pool, S32 pass)
{
    (void)pool; // unused - this body needs no per-instance state
    (void)pass; // unused - only ever called with a single (non-rigged) pass here
    LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);

    // S24 (2026-08-09, task #170): now loops twice (static + rigged, i==0/1)
    // matching lldrawpoolbump.cpp's renderDeferred() exactly - was skipped
    // entirely until task #168 fixed DXVertexLayout's MAP_WEIGHT4 rejection.
    for (int pass_i = 0; pass_i < 2; ++pass_i)
    {
        bool rigged = (pass_i == 1);
        gDeferredBumpProgram.bind(rigged);

        // S24 (2026-08-09): CORRECTION - the "no per-material texture-channel
        // registration" reasoning below predates the general D3D11-
        // reflection-based texture-channel fix built for the PBR texture-bind
        // bug (see project_dxrender_stage8_status memory) -
        // LLGLSLShader::enableTexture() now resolves a real channel from
        // shader reflection for ANY named texture the bound shader actually
        // declares, not just diffuse. Confirmed bumpF.hlsl declares and
        // samples `Texture2D bumpMap : register(t1)` for real (not a stub),
        // and LLDrawPoolBump::bindBumpMap() (lldrawpoolbump.cpp) - the
        // function that resolves a bump_code (brightness/darkness-derived OR
        // one of the ~17 standard library patterns like woodgrain/bark/brick)
        // to the right texture and binds it - is entirely backend-agnostic,
        // no #ifdef DX_RENDER anywhere in it. So the fix is exactly what GL
        // already does: get a real bump_channel and call bindBumpMap() per
        // drawinfo. diffuse_channel is intentionally still hardcoded to unit
        // 0 below (not switched to enableTexture(DIFFUSE_MAP)) - bumpF.hlsl
        // confirmed at t0 already, so this was never actually wrong, just
        // wasn't proven so before. Found via a user-compiled deep-dive report
        // + live testing (all bump variations rendering uniformly grey - the
        // exact symptom this explains).
        S32 bump_channel = LLGLSLShader::sCurBoundShaderPtr->enableTexture(LLViewerShaderMgr::BUMP_MAP);
        if (bump_channel > -1)
        {
            gGL.getTexUnit(bump_channel)->unbind(LLTexUnit::TT_TEXTURE);
        }

        const U32 type = rigged ? LLRenderPass::PASS_BUMP_RIGGED : LLRenderPass::PASS_BUMP;
        LLCullResult::drawinfo_iterator begin = gPipeline.beginRenderMap(type);
        LLCullResult::drawinfo_iterator end = gPipeline.endRenderMap(type);

        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;

        for (LLCullResult::drawinfo_iterator i = begin; i != end; )
        {
            LLDrawInfo& params = **i;
            LLCullResult::increment_iterator(i, end);

            LLGLSLShader::sCurBoundShaderPtr->setMinimumAlpha(params.mAlphaMaskCutoff);
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
                gGL.getTexUnit(0)->bindFast(params.mTexture);
            }
            else
            {
                gGL.getTexUnit(0)->unbindFast(LLTexUnit::TT_TEXTURE);
            }

            params.mVertexBuffer->setBuffer();
            params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
        }

        if (bump_channel > -1)
        {
            LLGLSLShader::sCurBoundShaderPtr->disableTexture(LLViewerShaderMgr::BUMP_MAP);
        }
        LLGLSLShader::sCurBoundShaderPtr->unbind();
        gGL.getTexUnit(0)->activate();
    }
}

// static
void DXDrawPoolBump::renderPostDeferred(LLDrawPoolBump& pool, S32 pass)
{
    (void)pass; // unused - only ever called with a single (non-rigged) pass here
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    // S24 (2026-08-09, task #170): now runs 2 passes (static + rigged)
    // unless rendering HUDs, matching lldrawpoolbump.cpp's
    // renderPostDeferred() exactly - was skipped entirely until task #168
    // fixed DXVertexLayout's MAP_WEIGHT4 rejection.
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
