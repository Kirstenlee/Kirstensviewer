/**
 * @file llgpuresize.cpp
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

#include "llgpuresize.h"

#include "pipeline.h"
#include "llrendertarget.h"
#include "llhlslshader.h"
#include "llviewershadermgr.h"
#include "llrender.h"

// S24 (2026-08-26, task #263): see llgpuresize.h's class comment and
// resizeBicubic.hlsl's own comment for the full design. Structure mirrors
// LLPipeline::generateGlow()'s two-pass ping-pong (pipeline.cpp) - the
// established, already-proven cross-backend shape for "run a separable
// shader pass over a render target" in this codebase - rather than hand-
// rolling raw D3D11 pipeline state.
bool LLGPUResize::resize(LLRenderTarget* src, LLRenderTarget* dst)
{
    if (!src || !dst || !src->isComplete() || !dst->isComplete())
    {
        return false;
    }

    if (!gResizeBicubicProgram.isComplete())
    {
        // Shader failed to compile - fail loudly to the caller rather than
        // silently leaving dst with whatever garbage/stale content it had.
        return false;
    }

    const U32 srcW = src->getWidth();
    const U32 srcH = src->getHeight();
    const U32 dstW = dst->getWidth();
    const U32 dstH = dst->getHeight();

    // Intermediate: horizontal pass resizes X only (srcW -> dstW), keeping
    // Y at the source height; the vertical pass then resizes Y only
    // (srcH -> dstH) from this into dst. One-shot allocation per call -
    // this is not a hot path (snapshot capture, at most a handful of times
    // per session), matching DXReadback's own "simplicity over pooling"
    // precedent (dxrender/resources/DXReadback.h's header comment).
    LLRenderTarget mid;
    if (!mid.allocate(dstW, srcH, GL_RGBA, false))
    {
        return false;
    }

    gResizeBicubicProgram.bind();

    // Pass 1: horizontal. Reads src (width srcW), writes mid (width dstW,
    // height srcH still). glowDelta = 1 SRC texel's UV step along X - see
    // resizeBicubic.hlsl's comment for why this must be the SOURCE of the
    // pass being run, not a fixed src/dst ratio.
    mid.bindTarget(false);
    gResizeBicubicProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, false, LLTexUnit::TFO_POINT);
    gResizeBicubicProgram.uniform2f(LLShaderMgr::GLOW_DELTA, 1.f / (F32)srcW, 0.f);
    gPipeline.mScreenTriangleVB->setBuffer();
    gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    mid.flush();

    // Pass 2: vertical. Reads mid (height srcH), writes dst (height dstH).
    // glowDelta = 1 texel's UV step along Y in MID (this pass's actual
    // source), not src or dst.
    dst->bindTarget(false);
    gResizeBicubicProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mid, false, LLTexUnit::TFO_POINT);
    gResizeBicubicProgram.uniform2f(LLShaderMgr::GLOW_DELTA, 0.f, 1.f / (F32)srcH);
    gPipeline.mScreenTriangleVB->setBuffer();
    gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    dst->flush();

    gResizeBicubicProgram.unbind();
    mid.release();

    return true;
}
