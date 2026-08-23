/**
 * @file dxdrawpoolwater.h
 * @brief Fresh DX11-native implementation of LLDrawPoolWater's post-deferred
 * water surface render path.
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

#pragma once

class LLDrawPoolWater;

// S24 (2026-08-04, task #109): staged full duplicate, same pattern as
// DXDrawPoolBump/DXDrawPoolAlpha - chosen over in-place edits per the same
// established preference (API separation between pools pays off once GL
// is retired).
//
// Unlike bump/alpha when they were first converted, almost nothing here
// needed a genuine behavior change - this water code (rewritten fairly
// recently, see the "Geenz 2025-02-11" comments in the GL source) already
// routes everything through already-DX-safe wrappers: bindDeferredShader()/
// unbindDeferredShader(), LLGLSLShader::bindTexture()/uniform*(),
// LLGLDepthTest/LLGLDisable, LLRender::setColorMask(), LLFace::renderIndexed()
// (via pushWaterPlanes(), unchanged - reused directly, not duplicated).
//
// One real, necessary gap found and fixed while converting this (not
// water-specific, a shared chokepoint): LLGLSLShader::bindTexture(S32,
// LLRenderTarget*, ...) - which this pool uses for WATER_SCREENTEX
// (mWaterDis, the screen-space reflection/refraction grab) and
// WATER_EXCLUSIONTEX (mWaterExclusionMask) - was a hardcoded DX_RENDER
// no-op (llglslshader.cpp). Fixed there, mirroring the already-fixed
// bindTexture(S32, LLTexture*, ...) overload.
//
// LLDrawPoolWaterExclusion (the invisiprim exclusion-mask pass) got wired
// in separately, task #116 (2026-08-09, see DXPipeline::renderGeomPostDeferred()'s
// own comment) - its render() needed zero changes (already composed
// entirely of DX-safe primitives), just a real call site plus a
// DX_RENDER-aware clear color fix in LLPipeline::doWaterExclusionMask()
// itself. mWaterExclusionMask is now filled every frame before this pool's
// WATER_EXCLUSIONTEX bind reads it.
class DXDrawPoolWater
{
public:
    static void beginPostDeferredPass(LLDrawPoolWater& pool, S32 pass);
    static void renderPostDeferred(LLDrawPoolWater& pool, S32 pass);
};
