/**
 * @file dxdrawpoolsimple.cpp
 * @brief Fresh DX11-native implementations of lldrawpoolsimple.h's 5 pool
 * classes that don't have their own in-place DX_RENDER branch yet.
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

#include "dxdrawpoolsimple.h"

#include "lldrawpoolsimple.h"
#include "llrender.h"
#include "llviewershadermgr.h"
#include "pipeline.h"

// static
void DXDrawPoolSimple::renderGrassDeferred(LLDrawPoolGrass& pool, S32 pass)
{
    gDeferredNonIndexedDiffuseAlphaMaskProgram.bind();
    gDeferredNonIndexedDiffuseAlphaMaskProgram.setMinimumAlpha(0.5f);

    // render grass - no rigged variant exists for this pool (grass isn't
    // skinned geometry), so unlike the other 4 pools this one has nothing
    // to skip.
    pool.pushBatches(LLRenderPass::PASS_GRASS, pool.getVertexDataMask());
}

// static
void DXDrawPoolSimple::renderAlphaMaskDeferred(LLDrawPoolAlphaMask& pool, S32 pass)
{
    LLHLSLShader* shader = &gDeferredDiffuseAlphaMaskProgram;

    // render static
    shader->bind();
    pool.pushMaskBatches(LLRenderPass::PASS_ALPHA_MASK, true, true);

    // S24 (2026-08-09, task #170): render rigged - was skipped until
    // task #168 fixed DXVertexLayout's MAP_WEIGHT4 rejection.
    shader->bind(true);
    pool.pushRiggedMaskBatches(LLRenderPass::PASS_ALPHA_MASK_RIGGED, true, true);
}

// static
void DXDrawPoolSimple::renderFullbrightAlphaMaskPostDeferred(LLDrawPoolFullbrightAlphaMask& pool, S32 pass)
{
    // LL::GLTFSceneManager rendering skipped - separate, unconverted
    // subsystem, explicitly deferred (see dxdrawpoolsimple.h class comment).

    LLHLSLShader* shader = nullptr;
    if (LLPipeline::sRenderingHUDs)
    {
        shader = &gHUDFullbrightAlphaMaskProgram;
    }
    else
    {
        shader = &gDeferredFullbrightAlphaMaskProgram;
    }

    LLGLDisable blend(GL_BLEND);

    // render static
    shader->bind();
    pool.pushMaskBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, true, true);

    // S24 (2026-08-09, task #170): render rigged - was skipped until
    // task #168 fixed DXVertexLayout's MAP_WEIGHT4 rejection.
    if (!LLPipeline::sRenderingHUDs)
    {
        shader->bind(true);
        pool.pushRiggedMaskBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK_RIGGED, true, true);
    }
}

// static
void DXDrawPoolSimple::renderFullbrightPostDeferred(LLDrawPoolFullbright& pool, S32 pass)
{
    LLHLSLShader* shader = nullptr;
    if (LLPipeline::sRenderingHUDs)
    {
        shader = &gHUDFullbrightProgram;
    }
    else
    {
        shader = &gDeferredFullbrightProgram;
    }

    gDX.setSceneBlendType(LLRender::BT_ALPHA);

    // render static
    shader->bind();
    pool.pushBatches(LLRenderPass::PASS_FULLBRIGHT, true, true);

    // S24 (2026-08-09, task #170): render rigged - was skipped until
    // task #168 fixed DXVertexLayout's MAP_WEIGHT4 rejection.
    if (!LLPipeline::sRenderingHUDs)
    {
        shader->bind(true);
        pool.pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_RIGGED, true, true);
    }
}

// static
void DXDrawPoolSimple::renderGlowPostDeferred(LLDrawPoolGlow& pool, S32 pass)
{
    LLHLSLShader* shader = &gDeferredEmissiveProgram;

    LLGLEnable blend(GL_BLEND);
    gDX.flush();

    // S24 (2026-08-28, task #242): real again via LLRender::setPolygonOffset()
    // (llrender.cpp) - biases depth to avoid z-fighting with the non-glow
    // pass, previously skipped entirely ("D3D11 has no runtime-callable
    // equivalent").
    LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
    gDX.setPolygonOffset(-1.0f, -1.0f);
    gDX.setSceneBlendType(LLRender::BT_ADD);

    LLGLDepthTest depth(GL_TRUE, GL_FALSE);
    gDX.setColorMask(false, true);

    // render static
    shader->bind();
    pool.pushBatches(LLRenderPass::PASS_GLOW, true, true);

    // S24 (2026-08-09, task #170): render rigged - was skipped until
    // task #168 fixed DXVertexLayout's MAP_WEIGHT4 rejection.
    shader = shader->mRiggedVariant;
    shader->bind();
    pool.pushRiggedBatches(LLRenderPass::PASS_GLOW_RIGGED, true, true);

    gDX.setColorMask(true, false);
    gDX.setSceneBlendType(LLRender::BT_ALPHA);
}
