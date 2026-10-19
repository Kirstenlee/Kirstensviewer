/**
 * @file dxdrawpoolsimple.cpp
 * @brief The 6 "simple" draw pool classes' constructors and render methods -
 * see dxdrawpoolsimple.h.
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

#include "llrender.h"
#include "llviewershadermgr.h"
#include "pipeline.h"
#include "dxpipeline.h"

static LLTrace::BlockTimerStatHandle FTM_RENDER_SIMPLE_DEFERRED("Deferred Simple");

LLDrawPoolSimple::LLDrawPoolSimple() :
    LLRenderPass(POOL_SIMPLE)
{
}

S32 LLDrawPoolSimple::getNumDeferredPasses()
{
    return 1;
}

void LLDrawPoolSimple::renderDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_SIMPLE_DEFERRED);
    DXDrawPoolSimple::renderSimpleDeferred(*this, pass);
}

// static
void DXDrawPoolSimple::renderSimpleDeferred(LLDrawPoolSimple& pool, S32 pass)
{
    LLGLDisable blend(GL_BLEND);

    // Rigged (skinned) geometry needs WEIGHT/WEIGHT4/JOINT vertex
    // attributes, supported via DXVertexLayout's MAP_WEIGHT4. Renders both
    // passes.
    gDeferredDiffuseProgram.bind();
    pool.pushBatches(LLRenderPass::PASS_SIMPLE, true, true);

    gDeferredDiffuseProgram.bind(true);
    pool.pushRiggedBatches(LLRenderPass::PASS_SIMPLE_RIGGED, true, true);
}

LLDrawPoolGrass::LLDrawPoolGrass() :
    LLRenderPass(POOL_GRASS)
{
}

void LLDrawPoolGrass::renderDeferred(S32 pass)
{
    DXDrawPoolSimple::renderGrassDeferred(*this, pass);
}

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

static LLTrace::BlockTimerStatHandle FTM_RENDER_ALPHA_MASK_DEFERRED("Deferred Alpha Mask");

LLDrawPoolAlphaMask::LLDrawPoolAlphaMask() :
    LLRenderPass(POOL_ALPHA_MASK)
{
}

void LLDrawPoolAlphaMask::renderDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_ALPHA_MASK_DEFERRED);
    DXDrawPoolSimple::renderAlphaMaskDeferred(*this, pass);
}

// static
void DXDrawPoolSimple::renderAlphaMaskDeferred(LLDrawPoolAlphaMask& pool, S32 pass)
{
    LLHLSLShader* shader = &gDeferredDiffuseAlphaMaskProgram;

    // render static
    shader->bind();
    pool.pushMaskBatches(LLRenderPass::PASS_ALPHA_MASK, true, true);

    shader->bind(true);
    pool.pushRiggedMaskBatches(LLRenderPass::PASS_ALPHA_MASK_RIGGED, true, true);
}

LLDrawPoolFullbrightAlphaMask::LLDrawPoolFullbrightAlphaMask() :
    LLRenderPass(POOL_FULLBRIGHT_ALPHA_MASK)
{
}

void LLDrawPoolFullbrightAlphaMask::renderPostDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_FULLBRIGHT);
    DXDrawPoolSimple::renderFullbrightAlphaMaskPostDeferred(*this, pass);
}

// static
void DXDrawPoolSimple::renderFullbrightAlphaMaskPostDeferred(LLDrawPoolFullbrightAlphaMask& pool, S32 pass)
{
    // LL::GLTFSceneManager rendering skipped - separate, unconverted
    // subsystem, explicitly deferred (see dxdrawpoolsimple.h class comment).

    LLHLSLShader* shader = nullptr;
    if (DXPipeline::sRenderingHUDs)
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

    if (!DXPipeline::sRenderingHUDs)
    {
        shader->bind(true);
        pool.pushRiggedMaskBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK_RIGGED, true, true);
    }
}

LLDrawPoolFullbright::LLDrawPoolFullbright() :
    LLRenderPass(POOL_FULLBRIGHT)
{
}

void LLDrawPoolFullbright::renderPostDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_FULLBRIGHT);
    DXDrawPoolSimple::renderFullbrightPostDeferred(*this, pass);
}

// static
void DXDrawPoolSimple::renderFullbrightPostDeferred(LLDrawPoolFullbright& pool, S32 pass)
{
    LLHLSLShader* shader = nullptr;
    if (DXPipeline::sRenderingHUDs)
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

    if (!DXPipeline::sRenderingHUDs)
    {
        shader->bind(true);
        pool.pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_RIGGED, true, true);
    }
}

void LLDrawPoolGlow::renderPostDeferred(S32 pass)
{
    DXDrawPoolSimple::renderGlowPostDeferred(*this, pass);
}

// static
void DXDrawPoolSimple::renderGlowPostDeferred(LLDrawPoolGlow& pool, S32 pass)
{
    LLHLSLShader* shader = &gDeferredEmissiveProgram;

    LLGLEnable blend(GL_BLEND);
    gDX.flush();

    // Biases depth via setPolygonOffset() to avoid z-fighting with the non-glow pass.
    LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
    gDX.setPolygonOffset(-1.0f, -1.0f);
    gDX.setSceneBlendType(LLRender::BT_ADD);

    DXDepthTest depth(true, false);
    gDX.setColorWriteMask(false, true);

    // render static
    shader->bind();
    pool.pushBatches(LLRenderPass::PASS_GLOW, true, true);

    shader = shader->mRiggedVariant;
    shader->bind();
    pool.pushRiggedBatches(LLRenderPass::PASS_GLOW_RIGGED, true, true);

    gDX.setColorWriteMask(true, false);
    gDX.setSceneBlendType(LLRender::BT_ALPHA);
}
