/**
 * @file dxpipeline.h
 * @brief Fresh DX11-native implementation of LLPipeline's deferred
 * render-loop drawing logic.
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

class LLCamera;
class LLPipeline;

// NOT a full parallel pipeline. LLPipeline (pipeline.cpp) stays the single
// source of truth for orchestration - culling, spatial partition traversal,
// visibility, render-type flags - for BOTH backends; that code is CPU-only
// bookkeeping with no GL calls in it, and forking it would mean manually
// re-porting every future upstream LLPipeline change into a second,
// structurally-different copy forever. DXPipeline only owns the "given this
// visible-pool-set, issue draw calls" layer, called from LLPipeline's
// existing entry points via a thin #ifdef DX_RENDER redirect (same pattern
// used throughout this stage - see LLHLSLShader::bind()/createShader() for
// precedent), rather than fencing pipeline.cpp's dense GL body in place.
//
// Scope note: only draw pools explicitly whitelisted in the .cpp are drawn -
// everything else is silently skipped (not a bug; those pools haven't been
// converted to DX_RENDER yet). Expand the whitelist as more pools convert.
// This first pass also skips wireframe mode, hardware-light setup, stereo
// color-mask modes, reflection-probe uniform updates, and occlusion culling
// - those are separate, still-unconverted concerns, not part of "draw the
// visible simple-pool geometry" itself.
class DXPipeline
{
public:
    // Mirrors LLPipeline::renderGeomDeferred()'s GL loop, simplified per the
    // class comment above. do_occlusion triggers a real DXOcclusionQuery-
    // backed occlusion pass in the .cpp, matching GL's own POOL_GRASS
    // threshold.
    static void renderGeomDeferred(LLPipeline& pipeline, LLCamera& camera, bool do_occlusion);

    // Mirrors LLPipeline::renderGeomPostDeferred()'s GL loop (the "forward"/
    // translucent pass - windows, glass, glow, eventually water) - same
    // simplification philosophy, see dxpipeline.cpp's isConvertedPostDeferredPool()
    // for exactly which pools are wired up so far.
    static void renderGeomPostDeferred(LLPipeline& pipeline, LLCamera& camera);

    // Called from LLPipeline::renderFinalize()'s DX_RENDER branch - the real
    // post-fx chain (tasks #137-141): gamma-correct/tonemap -> glow -> DoF
    // (optional) -> FXAA/SMAA (optional) -> real final present
    // (gDeferredPostNoDoFNoiseProgram, depth-aware noise dithering) -> back
    // buffer, mirroring LLPipeline::renderFinalize()'s GL body stage-for-
    // stage (pipeline.cpp:8482-8623). Falls back to a raw unlit
    // deferredScreen blit (via presentFinal(), dxpipeline.cpp) whenever
    // renderDeferredLighting() didn't complete this frame or the gamma
    // shader failed to compile, so a shader/lighting gap degrades
    // gracefully instead of showing a blank window.
    // HDR auto-exposure (generateLuminance()/generateExposure(), dynamic eye
    // adaptation) is still a no-op under DX_RENDER (see the .cpp) - the
    // tonemap curve itself is correct but exposure never adapts to scene
    // brightness.
    static void presentDeferredScreen(LLPipeline& pipeline);

    // The ONE place the two eyes' color ever actually mixes. Call once,
    // after both eyes' presentDeferredScreen() passes have captured into
    // pipeline.mStereoEyeL/mStereoEyeR (see getCurrentStereoEyeTarget()'s
    // comment in the .cpp), right before the frame's single swap(). No-ops
    // safely if either eye target has nothing in it yet.
    static void presentStereoComposite(LLPipeline& pipeline);

    // Mirrors LLPipeline::renderDeferredLighting()'s GL body: the
    // ambient+sun/atmospherics term (softenLightF/V.hlsl) via the
    // bindDeferredShader() chokepoint, writing the lit result into
    // mRT->screen; also covers the sun-shadow/SSAO lightmap pass and the
    // local point/spot light loop further down in the .cpp.
    // presentDeferredScreen() falls back to its existing raw deferredScreen
    // blit whenever gDeferredSoftenProgram isn't complete, so a shader
    // compile failure here degrades gracefully instead of breaking the
    // frame.
    static void renderDeferredLighting(LLPipeline& pipeline);

    // SMAA.hlsl's own LinearSampler(s13)/PointSampler(s14) - must be bound before every SMAA
    // pass (edge-detect, blend-weights, neighborhood-blend); nothing else in this codebase
    // touches these slots. Was 3 duplicated inline snippets across LLPipeline::
    // generateSMAABuffers()/applySMAA() - task #290's consolidation pass.
    static void bindSMAAStaticSamplers();
};
