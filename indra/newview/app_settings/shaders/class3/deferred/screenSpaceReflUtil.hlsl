/**
 * @file class3/deferred/screenSpaceReflUtil.hlsl
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

// S24 (2026-08-11, task #156): real implementation, replacing the debug
// stub - mechanical port of screenSpaceReflUtil.glsl (based on
// https://github.com/RoundedGlint585/ScreenSpaceReflection/), the only
// consumer of which is class3/deferred/reflectionProbeF.hlsl's
// doProbeSample()/sampleReflectionProbesLegacy() (their own #ifdef SSR
// blocks, restored alongside this file). See the reflection-probe pipeline
// plan (project memory / session plan file, task #156 section) for the
// full investigation - the standalone gPostScreenSpaceReflectionProgram
// (screenSpaceReflPostV/F.hlsl) is confirmed dead on both backends and is
// NOT what this feeds; nor are screenSpaceReflTraceF/FilterF/AlphaV/F/
// WaterF.hlsl (also confirmed dead, unreferenced by any registered shader
// program).

Texture2D sceneDepth : register(t18);
// S24: sceneDepth reuses deferredUtil.hlsl's depthMapSampler (s1) rather
// than declaring a new sampler - D3D11 pixel-shader sampler slots are
// hard-capped at 16 (s0-s15), already hit and fixed once for
// reflectionProbes/irradianceProbes at t16/t17 (see reflectionProbeF.hlsl's
// own comment on that exact bug class, task #114/#147b). deferredUtil.hlsl
// is always co-attached wherever this file is (SSR only matters for
// deferred PBR lighting), so depthMapSampler is guaranteed present - a
// point-filtered sampler is also the semantically correct choice here,
// matching how depthMap itself is always sampled. Guarded with
// deferredUtil.hlsl's own LL_DEPTHMAP_DECLARED macro (not just a bare
// second declaration) so this doesn't X3003-redefine it regardless of
// concatenation order - an earlier unguarded copy caused a startup CTD
// via "Water Shader" failing to compile (task #156 hotfix).
#ifndef LL_DEPTHMAP_DECLARED
#define LL_DEPTHMAP_DECLARED
uniform SamplerState depthMapSampler : register(s1);
#endif

// S24: inv_proj/screen_res are also declared, as a pair, by deferredUtil.hlsl
// under LL_INV_PROJ_DECLARED - reused here under the same guard (not a bare
// second declaration) for the same reason as depthMapSampler above; an
// earlier unguarded copy of these two caused a second X3003 startup CTD
// ("redefinition of 'screen_res'" in Water Shader, task #156 hotfix round 2).
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif
uniform float4x4 projection_matrix;
// S24 (2026-09-06, task #271 rebuild): last_projection_matrix - the SAME
// dedicated uniform temporalResolveSSAOF.hlsl (task #190, a real, shipping,
// proven-working reprojection feature solving almost this exact problem)
// uses when projecting a position that has already been moved into
// last-frame view space. Never previously declared/uploaded for any
// reflection-probe/SSR-consuming shader - see LLPipeline::bindReflectionProbes()
// (pipeline.cpp) for the matching new upload, added alongside this.
uniform float4x4 last_projection_matrix;
uniform float4x4 modelview_delta;    // transform from last camera space to current camera space
uniform float4x4 inv_modelview_delta;

float4 getPositionWithDepth(float2 pos_screen, float depth);

float random(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453123); //simple random function
}

// Based off of https://github.com/RoundedGlint585/ScreenSpaceReflection/
// A few tweaks here and there to suit our needs.

// S24 (2026-09-06, task #271 rebuild): CAMERA MODULE. Single low-level
// forward-projection primitive - view-space position + an explicit
// projection matrix -> screen UV. Two real, structural bugs fixed here
// relative to the original ported-from-GL version:
//
// 1) No w>0 guard before the perspective divide. Every proven-working
//    reprojection helper in this engine (getScreenCoord() in
//    deferredUtil.hlsl, temporalResolveSSAOF.hlsl's own forward-projection
//    step) checks this before dividing - dividing unconditionally is
//    exactly what produces wild/inverted UVs once w goes small or negative,
//    which is what tonight's diagnostics (SSR7/9/10) actually measured.
//    Returns a sentinel float2(-1,-1) on failure - guaranteed to fail the
//    existing [0,1] bounds check at every call site with no further changes
//    needed there.
// 2) No baked-in Y-flip. getScreenCoord()/getPositionWithNDC() (the proven,
//    shared primitives) never flip Y - they leave that entirely to the
//    actual .Sample() call site (getDepth()'s own established pattern).
//    The old version baked a flip into its return value, which was then
//    fed into getPositionWithDepth() (which expects an UNFLIPPED UV) by
//    getLinearDepth() below - a real, silent vertical-mirror bug in the
//    reconstructed position for anything but a perfectly symmetric case.
//    Flip is now applied ONLY at the texture .Sample() call sites that
//    actually need it (getLinearDepth() below, and the final hit-color
//    reads), matching the rest of this codebase exactly.
bool ssrProject(float3 pos, float4x4 proj, out float2 uv)
{
    float4 clip = mul(proj, float4(pos, 1.f));
    if (clip.w <= 0.0)
    {
        uv = float2(-1, -1);
        return false;
    }
    uv = (clip.xy / clip.w) * 0.5 + 0.5;
    return true;
}

// External contract preserved exactly (pbralphaF.hlsl calls this by name)
// - projects a CURRENT-frame view-space position using the current frame's
// own projection_matrix. Callers of this specific function only ever use
// the result for flip-invariant purposes (vignette falloff, jitter seed),
// never a direct texture sample, so the unflipped convention here is safe
// for that existing external use.
float2 generateProjectedPosition(float3 pos)
{
    float2 uv;
    ssrProject(pos, projection_matrix, uv);
    return uv;
}

static bool isBinarySearchEnabled = true;
static bool isAdaptiveStepEnabled = true;
static bool isExponentialStepEnabled = true;
static bool debugDraw = false;

uniform float iterationCount;
uniform float rayStep;
uniform float distanceBias;
uniform float depthRejectBias;
uniform float glossySampleCount;
uniform float adaptiveStepMultiplier;
uniform float noiseSine;

static float epsilon = 0.1;
// S24 (2026-09-06, task #271): hard cap on how far a ray is allowed to
// march before giving up (world units, measured along the marching
// position's own Z as it travels). Mild first pass, live-tuning - stops
// the ray before it reaches the far, coarsely-stepped zone where hits
// start looking banded/distorted, trading reach for crispness on what
// does resolve. Misses beyond this range fall back to nothing (the cube
// contribution is intentionally zeroed for SSR-eligible surfaces, see
// reflectionProbeF.hlsl's doProbeSample()), not a graceful cube fallback -
// accepted tradeoff for now, revisit if a softer falloff is wanted later.
static float maxReflectionDepth = 6.0;

// S24 (2026-09-06, task #271 rebuild): DEPTH MODULE. tc here is the
// UNFLIPPED convention (matches ssrProject()'s return and
// getPositionWithDepth()'s own expectation) - the Y-flip is applied ONLY at
// this actual .Sample() call, exactly matching deferredUtil.hlsl's
// getDepth()/getNorm() pattern (D3D11 top-left vs GL bottom-left texture
// origin - see that file's own comment). getPositionWithDepth() itself is
// the same proven, reversed-Z-aware primitive SSAO/shadows/lighting already
// use successfully - not reimplemented here, just fed the correct
// (unflipped) UV this time.
float getLinearDepth(float2 tc)
{
    // S24: SampleLevel (explicit LOD 0), not Sample - this is called from
    // inside traceScreenRay()'s ray-march loop, whose trip count
    // (iterationCount) is a runtime uniform, not a compile-time constant.
    // Sample()'s implicit screen-space gradient computation requires the
    // compiler to fully unroll any containing loop (X3570), which then
    // fails outright since the trip count isn't statically known (X3511,
    // task #156 hotfix round 4). SampleLevel has no gradient requirement,
    // so the loop can stay a real dynamic [loop] instead.
    float depth = sceneDepth.SampleLevel(depthMapSampler, float2(tc.x, 1.0 - tc.y), 0).r;

    float4 pos = getPositionWithDepth(tc, depth);

    return -pos.z;
}

bool traceScreenRay(float3 position, float3 reflection, out float4 hitColor, out float hitDepth, float depth, Texture2D textureFrame, SamplerState textureFrameSampler)
{
    // transform position and reflection into same coordinate frame as the sceneMap and sceneDepth
    reflection += position;
    position = mul(inv_modelview_delta, float4(position, 1)).xyz;
    reflection = mul(inv_modelview_delta, float4(reflection, 1)).xyz;
    reflection -= position;

    depth = -position.z;

    float3 step = rayStep * reflection;
    float3 marchingPosition = position + step;
    float delta;
    float depthFromScreen;
    float2 screenPosition;
    bool hit = false;
    hitColor = float4(0, 0, 0, 0);
    hitDepth = 0.0;

    int i = 0;
    if (depth > depthRejectBias)
    {
        // S24: [loop] forces a real dynamic loop instead of the compiler's
        // default attempt to fully unroll (X3570/X3511, task #156 hotfix
        // round 4) - iterationCount is a runtime uniform, so the trip count
        // is never statically known and unrolling can't succeed. Legal now
        // that every texture read in this loop uses SampleLevel (no
        // gradient requirement), not Sample.
        [loop]
        for (; i < (int)iterationCount && !hit; i++)
        {
            // S24 (2026-09-06, task #271): max-reflection-depth cap - stop
            // before the ray reaches the far, coarsely-stepped zone at all.
            if (abs(marchingPosition.z - position.z) > maxReflectionDepth)
            {
                hit = false;
                break;
            }
            // S24 (2026-09-06, task #271 rebuild): marchingPosition lives in
            // LAST-FRAME view space (transformed once, above) - projecting
            // it needs last_projection_matrix, not the current frame's
            // projection_matrix (the old code's real bug: mismatched space
            // and matrix). ssrProject()'s w>0 guard replaces the implicit
            // "hope it's in bounds" the old unconditional divide relied on.
            if (!ssrProject(marchingPosition, last_projection_matrix, screenPosition))
            {
                hit = false;
                break;
            }
            if (screenPosition.x > 1 || screenPosition.x < 0 ||
                screenPosition.y > 1 || screenPosition.y < 0)
            {
                hit = false;
                break;
            }
            depthFromScreen = getLinearDepth(screenPosition);
            delta = abs(marchingPosition.z) - depthFromScreen;

            if (depth < depthFromScreen + epsilon && depth > depthFromScreen - epsilon)
            {
                break;
            }

            if (abs(delta) < distanceBias)
            {
                float4 color = float4(1, 1, 1, 1);
                if (debugDraw)
                    // S24 (2026-09-02): was `sign(delta) / 2` - HLSL's
                    // sign() always returns int regardless of input type
                    // (per the intrinsic spec), so this was integer
                    // division of -1/0/1 by 2 - truncates to 0 every time,
                    // a real logic bug (this debugDraw-only overlay could
                    // never actually shift color by delta's sign, always
                    // rendered the same flat color), not just the X3556
                    // perf warning it also happened to trigger. Dividing by
                    // 2.0 promotes to a real float divide.
                    color = float4(0.5 + sign(delta) / 2.0, 0.3, 0.5 - sign(delta) / 2.0, 0);
                // S24 (2026-09-06, task #271 rebuild): Y-flip applied here,
                // at the actual sample call, matching getDepth()'s
                // established pattern - screenPosition itself stays
                // unflipped (see ssrProject()'s own comment).
                hitColor = textureFrame.SampleLevel(textureFrameSampler, float2(screenPosition.x, 1.0 - screenPosition.y), 0) * color;
                hitDepth = depthFromScreen;

                // S24 (2026-09-07, task #266/#315 continuation): distance-
                // based confidence fade - live-reported (Cat2.png/floor.png)
                // as a smeared, "hall of mirrors" mess reflecting distant
                // ceiling/roof structure through a flat glass floor, NOT
                // fixed by two separate, real convergence-algorithm fixes
                // (step-length cap + growth-gating) that were live-tested
                // and made zero difference - ruling out the march's OWN
                // convergence behavior as the cause. A flat floor reflecting
                // distant content is close to the worst-case geometry for
                // screen-space ray marching: at that shallow/grazing angle,
                // a tiny error in where the fixed-threshold "close enough"
                // (distanceBias) crossing gets accepted corresponds to a
                // huge error in which actual scene surface gets sampled -
                // every hit up to now has been treated as equally reliable
                // right up to the hard maxReflectionDepth cutoff, with zero
                // distinction between a confident nearby hit and a marginal
                // one found only after marching most of the whole budget.
                // Fades hitColor toward black as travel distance approaches
                // maxReflectionDepth, instead of a hard cliff - this both
                // directly de-weights the far/grazing/unreliable hits most
                // likely to be wrong, AND (combined with the existing near-
                // black-hit rejection above in the caller) lets sufficiently-
                // faded distant hits fall back gracefully to the cube/
                // equalized probe content instead of showing full-confidence
                // wrong color.
                float travelDist = abs(marchingPosition.z - position.z);
                float distanceFade = 1.0 - saturate(travelDist / maxReflectionDepth);
                hitColor.rgb *= distanceFade * distanceFade;

                hit = true;
                break;
            }
            if (isBinarySearchEnabled && delta > 0)
            {
                break;
            }
            // S24 (2026-09-07, task #266/#315 continuation): tracks whether
            // this iteration just overshot the surface (see
            // isAdaptiveStepEnabled branch below) - used to gate the
            // exponential growth further down. Declared here (not inside
            // that branch) so it's still in scope there even when
            // isAdaptiveStepEnabled is off (stays 0, never suppresses
            // growth in that case - behavior for that combination is
            // unchanged).
            float overshotSign = 0.0;

            if (isAdaptiveStepEnabled)
            {
                float directionSign = sign(abs(marchingPosition.z) - depthFromScreen);
                //this is sort of adapting step, should prevent lining reflection by doing sort of iterative converging
                //some implementation doing it by binary search, but I found this idea more cheaty and way easier to implement
                step = step * (1.0 - rayStep * max(directionSign, 0.0));
                marchingPosition += step * (-directionSign);
                overshotSign = directionSign;
            }
            else
            {
                marchingPosition += step;
            }

            // S24 (2026-09-07, task #266/#315 continuation): live-reported
            // (Cat2.png, 1 gloss sample/25 iterations - a single ray with
            // no stochastic averaging to mask the underlying issue) as
            // "iterations just slide away into infinity producing a
            // smeared mess." Real structural gap beyond the step-length cap
            // above: exponential growth ran EVERY iteration unconditionally,
            // even right after the adaptive branch had just shrunk the step
            // to correct an overshoot - immediately undoing that correction
            // and letting the march re-accelerate past the true crossing
            // instead of converging onto it, a genuine non-convergence risk
            // (oscillate/overshoot progressively farther) independent of
            // the raw magnitude cap. A correctly-converging march should
            // only grow its step while still searching forward for the
            // first crossing; once it has overshot at least once, every
            // subsequent step should keep shrinking (the adaptive branch's
            // own (1-rayStep) factor already does this correctly on its
            // own) rather than being regrown. Gated below on
            // overshotSign<=0 - once true convergence-mode kicks in, no
            // more exponential growth.
            if (isExponentialStepEnabled && overshotSign <= 0.0)
            {
                step *= adaptiveStepMultiplier;

                // S24 (2026-09-07, task #266/#315 continuation): live-
                // reported (floor.png) as "fragmentary and distorted...
                // each depth iteration more offset than the last, with
                // distorted bends" - a "hall of mirrors" look that got
                // WORSE, not better, with more gloss samples or more
                // iterations, and was completely unaffected by camera
                // motion (confirmed stationary). Root cause: this
                // multiply had no cap at all - step compounds
                // exponentially every iteration with no ceiling, so by
                // later iterations a single step can jump a huge fraction
                // of the entire maxReflectionDepth budget in one go. Since
                // this runs once per independent stochastic sample (each
                // with a slightly different jittered direction), a tiny
                // direction difference between samples gets amplified by
                // the runaway exponential into landing on completely
                // unrelated surfaces at wildly different distances -
                // averaging those together is exactly the torn, multi-
                // layered ghosting reported. More samples/iterations only
                // ever made this worse (more divergent hits to average,
                // more room for the exponential to run away before the
                // total-distance safety check below catches it), which
                // matches why no amount of tuning helped. Capping the
                // single-step magnitude to a fraction of the total
                // maxReflectionDepth budget keeps the early-iteration
                // speedup (still reaches distant content efficiently) but
                // stops it from blowing up into an unpredictable single
                // jump later in the same march.
                float stepLen = length(step);
                float maxStepLen = maxReflectionDepth * 0.15;
                if (stepLen > maxStepLen)
                {
                    step *= maxStepLen / stepLen;
                }
            }
        }
        if (isBinarySearchEnabled)
        {
            [loop]
            for (; i < (int)iterationCount && !hit; i++)
            {
                step *= 0.5;
                marchingPosition = marchingPosition - step * sign(delta);

                // S24 (2026-09-06, task #271 rebuild): same fix as the main
                // loop above - last_projection_matrix, not projection_matrix,
                // plus the w>0 guard.
                if (!ssrProject(marchingPosition, last_projection_matrix, screenPosition))
                {
                    hit = false;
                    break;
                }
                if (screenPosition.x > 1 || screenPosition.x < 0 ||
                    screenPosition.y > 1 || screenPosition.y < 0)
                {
                    hit = false;
                    break;
                }
                depthFromScreen = getLinearDepth(screenPosition);
                delta = abs(marchingPosition.z) - depthFromScreen;

                if (depth < depthFromScreen + epsilon && depth > depthFromScreen - epsilon)
                {
                    break;
                }

                if (abs(delta) < distanceBias && depthFromScreen != (depth - distanceBias))
                {
                    float4 color = float4(1, 1, 1, 1);
                    if (debugDraw)
                        // S24 (2026-09-02): was `sign(delta) / 2` - HLSL's
                    // sign() always returns int regardless of input type
                    // (per the intrinsic spec), so this was integer
                    // division of -1/0/1 by 2 - truncates to 0 every time,
                    // a real logic bug (this debugDraw-only overlay could
                    // never actually shift color by delta's sign, always
                    // rendered the same flat color), not just the X3556
                    // perf warning it also happened to trigger. Dividing by
                    // 2.0 promotes to a real float divide.
                    color = float4(0.5 + sign(delta) / 2.0, 0.3, 0.5 - sign(delta) / 2.0, 0);
                    // S24 (2026-09-06, task #271 rebuild): same flip-at-
                    // sample fix as the main loop's hit above.
                    hitColor = textureFrame.SampleLevel(textureFrameSampler, float2(screenPosition.x, 1.0 - screenPosition.y), 0) * color;
                    hitDepth = depthFromScreen;

                    // S24 (2026-09-07, task #266/#315 continuation): same
                    // distance-based confidence fade as the coarse loop's
                    // hit above - see that comment for the full reasoning.
                    float travelDistRefined = abs(marchingPosition.z - position.z);
                    float distanceFadeRefined = 1.0 - saturate(travelDistRefined / maxReflectionDepth);
                    hitColor.rgb *= distanceFadeRefined * distanceFadeRefined;

                    hit = true;
                    break;
                }
            }
        }
    }

    return hit;
}

// S24: 128-entry Poisson-disc sample table, ported verbatim from the GLSL
// source (same values, vec3[128] literal-initializer array -> static
// const float3[128]).
static const float3 POISSON3D_SAMPLES[128] =
{
    float3(0.5433144, 0.1122154, 0.2501391),
    float3(0.6575254, 0.721409, 0.16286),
    float3(0.02888453, 0.05170321, 0.7573566),
    float3(0.06635678, 0.8286457, 0.07157445),
    float3(0.8957489, 0.4005505, 0.7916042),
    float3(0.3423355, 0.5053263, 0.9193521),
    float3(0.9694794, 0.9461077, 0.5406441),
    float3(0.9975473, 0.02789414, 0.7320132),
    float3(0.07781899, 0.3862341, 0.918594),
    float3(0.4439073, 0.9686955, 0.4055861),
    float3(0.9657035, 0.6624081, 0.7082613),
    float3(0.7712346, 0.07273269, 0.3292839),
    float3(0.2489169, 0.2550394, 0.1950516),
    float3(0.7249326, 0.9328285, 0.3352458),
    float3(0.6028461, 0.4424961, 0.5393377),
    float3(0.2879795, 0.7427881, 0.6619173),
    float3(0.3193627, 0.0486145, 0.08109283),
    float3(0.1233155, 0.602641, 0.4378719),
    float3(0.9800708, 0.211729, 0.6771586),
    float3(0.4894537, 0.3319927, 0.8087631),
    float3(0.4802743, 0.6358885, 0.814935),
    float3(0.2692913, 0.9911493, 0.9934899),
    float3(0.5648789, 0.8553897, 0.7784553),
    float3(0.8497344, 0.7870212, 0.02065313),
    float3(0.7503014, 0.2826185, 0.05412734),
    float3(0.8045461, 0.6167251, 0.9532926),
    float3(0.04225039, 0.2141281, 0.8678675),
    float3(0.07116079, 0.9971236, 0.3396397),
    float3(0.464099, 0.480959, 0.2775862),
    float3(0.6346927, 0.31871, 0.6588384),
    float3(0.449012, 0.8189669, 0.2736875),
    float3(0.452929, 0.2119148, 0.672004),
    float3(0.01506042, 0.7102436, 0.9800494),
    float3(0.1970513, 0.4713539, 0.4644522),
    float3(0.13715, 0.7253224, 0.5056525),
    float3(0.9006432, 0.5335414, 0.02206874),
    float3(0.9960898, 0.7961011, 0.01468861),
    float3(0.3386469, 0.6337739, 0.9310676),
    float3(0.1745718, 0.9114985, 0.1728188),
    float3(0.6342545, 0.5721557, 0.4553517),
    float3(0.1347412, 0.1137158, 0.7793725),
    float3(0.3574478, 0.3448052, 0.08741581),
    float3(0.7283059, 0.4753885, 0.2240275),
    float3(0.8293507, 0.9971212, 0.2747005),
    float3(0.6501846, 0.000688076, 0.7795712),
    float3(0.01149416, 0.4930083, 0.792608),
    float3(0.666189, 0.1875442, 0.7256873),
    float3(0.8538797, 0.2107637, 0.1547532),
    float3(0.5826825, 0.9750752, 0.9105834),
    float3(0.8914346, 0.08266425, 0.5484225),
    float3(0.4374518, 0.02987111, 0.7810078),
    float3(0.2287418, 0.1443802, 0.1176908),
    float3(0.2671157, 0.8929081, 0.8989366),
    float3(0.5425819, 0.5524959, 0.6963879),
    float3(0.3515188, 0.8304397, 0.0502702),
    float3(0.3354864, 0.2130747, 0.141169),
    float3(0.9729427, 0.3509927, 0.6098799),
    float3(0.7585629, 0.7115368, 0.9099342),
    float3(0.0140543, 0.6072157, 0.9436461),
    float3(0.9190664, 0.8497264, 0.1643751),
    float3(0.1538157, 0.3219983, 0.2984214),
    float3(0.8854713, 0.2968667, 0.8511457),
    float3(0.1910622, 0.03047311, 0.3571215),
    float3(0.2456353, 0.5568692, 0.3530164),
    float3(0.6927255, 0.8073994, 0.5808484),
    float3(0.8089353, 0.8969175, 0.3427134),
    float3(0.194477, 0.7985603, 0.8712182),
    float3(0.7256182, 0.5653068, 0.3985921),
    float3(0.9889427, 0.4584851, 0.8363391),
    float3(0.5718582, 0.2127113, 0.2950557),
    float3(0.5480209, 0.0193435, 0.2992659),
    float3(0.6598953, 0.09478426, 0.92187),
    float3(0.1385615, 0.2193868, 0.205245),
    float3(0.7623423, 0.1790726, 0.1508465),
    float3(0.7569032, 0.3773386, 0.4393887),
    float3(0.5842971, 0.6538072, 0.5224424),
    float3(0.9954313, 0.5763943, 0.9169143),
    float3(0.001311183, 0.340363, 0.1488652),
    float3(0.8167927, 0.4947158, 0.4454727),
    float3(0.3978434, 0.7106082, 0.002727509),
    float3(0.5459411, 0.7473233, 0.7062873),
    float3(0.4151598, 0.5614617, 0.4748358),
    float3(0.4440694, 0.1195122, 0.9624678),
    float3(0.1081301, 0.4813806, 0.07047641),
    float3(0.2402785, 0.3633997, 0.3898734),
    float3(0.2317942, 0.6488295, 0.4221864),
    float3(0.01145542, 0.9304277, 0.4105759),
    float3(0.3563728, 0.9228861, 0.3282344),
    float3(0.855314, 0.6949819, 0.3175117),
    float3(0.730832, 0.01478493, 0.5728671),
    float3(0.9304829, 0.02653277, 0.712552),
    float3(0.4132186, 0.4127623, 0.6084146),
    float3(0.7517329, 0.9978395, 0.1330464),
    float3(0.5210338, 0.4318751, 0.9721575),
    float3(0.02953994, 0.1375937, 0.9458942),
    float3(0.1835506, 0.9896691, 0.7919457),
    float3(0.3857062, 0.2682322, 0.1264563),
    float3(0.6319699, 0.8735335, 0.04390657),
    float3(0.5630485, 0.3339024, 0.993995),
    float3(0.90701, 0.1512893, 0.8970422),
    float3(0.3027443, 0.1144253, 0.1488708),
    float3(0.9149003, 0.7382028, 0.7914025),
    float3(0.07979286, 0.6892691, 0.2866171),
    float3(0.7743186, 0.8046008, 0.4399814),
    float3(0.3128662, 0.4362317, 0.6030678),
    float3(0.1133721, 0.01605821, 0.391872),
    float3(0.5185481, 0.9210006, 0.7889017),
    float3(0.8217013, 0.325305, 0.1668191),
    float3(0.8358996, 0.1449739, 0.3668382),
    float3(0.1778213, 0.5599256, 0.1327691),
    float3(0.06690693, 0.5508637, 0.07212365),
    float3(0.9750564, 0.284066, 0.5727578),
    float3(0.4350255, 0.8949825, 0.03574753),
    float3(0.8931149, 0.9177974, 0.8123496),
    float3(0.9055127, 0.989903, 0.813235),
    float3(0.2897243, 0.3123978, 0.5083504),
    float3(0.1519223, 0.3958645, 0.2640327),
    float3(0.6840154, 0.6463035, 0.2346607),
    float3(0.986473, 0.8714055, 0.3960275),
    float3(0.6819352, 0.4169535, 0.8379834),
    float3(0.9147297, 0.6144146, 0.7313942),
    float3(0.6554981, 0.5014008, 0.9748477),
    float3(0.9805915, 0.1318207, 0.2371372),
    float3(0.5980836, 0.06796348, 0.9941338),
    float3(0.6836596, 0.9917196, 0.2319056),
    float3(0.5276511, 0.2745509, 0.5422578),
    float3(0.829482, 0.03758276, 0.1240466),
    float3(0.2698198, 0.0002266169, 0.3449324)
};

float3 getPoissonSample(int i)
{
    return POISSON3D_SAMPLES[i] * 2 - 1;
}

float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness)
{
#ifdef TRANSPARENT_SURFACE
    collectedColor = float4(1, 0, 1, 1);
    return 0.f;
#endif
    collectedColor = float4(0, 0, 0, 0);
    int hits = 0;

    float depth = -viewPos.z;
    // S24 (2026-09-06, task #271): kept separately from `depth` below, which
    // gets overwritten with each iteration's own hit depth (see the
    // existing comment on the traceScreenRay() call further down) -
    // startDepth stays fixed at this surface point's own distance from the
    // camera, used to measure how far a found reflection is FROM the
    // surface itself (not from the camera) for the progressive-blur jitter
    // widening below.
    float startDepth = depth;

    float3 rayDirection = normalize(reflect(viewPos, normalize(n)));

    // S24: the GLSL original also computes a `jitter` value here
    // (uv2/c/jitter, based on tc*screen_res) that is never actually read
    // anywhere else in the function - confirmed dead in the source being
    // ported, not just this port. Omitted rather than carried over as
    // inert code (also sidesteps GLSL mod() vs HLSL fmod() sign-behavior
    // differences for a value that was never used).
    // S24 (2026-09-06, task #271 rebuild): now that the ray march itself is
    // fixed and proven to find genuine hits, this confidence weighting was
    // found to be far too conservative - up to 4 discount factors
    // multiplying together capped real, correct hits around ~10-15% blend
    // weight against the cube probe, which is why the cube visibly
    // dominated (including its own box-probe parallax-boundary artifacts)
    // even on a solid SSR hit. Two changes:
    // 1) The 3 remaining geometric falloff terms (screen-position, viewing
    //    angle, far-distance) are now sqrt()'d - keeps 0->0 and 1->1 but
    //    lifts mid-range values substantially, so a decent-but-not-perfect
    //    angle/position no longer gets crushed as hard.
    // 2) The 4th term (clamp(glossiness*3-1.7,0,1)) is removed outright -
    //    it's a SECOND, hardcoded, non-exposed glossiness gate, redundant
    //    with (and stricter than) reflectionProbeF.hlsl's already-exposed,
    //    user-tunable ssrGlossThreshold uniform, which every caller of this
    //    function has already had to clear before reaching here at all.
    float2 screenpos = 1 - abs(tc * 2 - 1);
    float vignette = sqrt(clamp((abs(screenpos.x) * abs(screenpos.y)) * 16, 0, 1));
    vignette *= sqrt(clamp((dot(normalize(viewPos), n) * 0.5 + 0.5) * 5.5 - 0.8, 0, 1));

    float zFar = 128.0;
    vignette *= sqrt(clamp(1.0 + (viewPos.z / zFar), 0.0, 1.0));

    float4 hitpoint;

    glossiness = 1 - glossiness;

    totalSamples = (int)max(glossySampleCount, glossySampleCount * glossiness * vignette);

    totalSamples = max(totalSamples, 1);
    // S24 (2026-09-05, task #271): tested theory that this hardcoded gate
    // (independent of reflectionProbeF.hlsl's tunable ssrGlossThreshold) was
    // silently zeroing the sampling loop for the floor material. Live-tested
    // forced to `if (true)` - no change to the floor symptom, theory
    // refuted. Restored to the original, upstream-faithful condition.
    if (glossiness < 0.35)
    {
        if (vignette > 0)
        {
            for (int i = 0; i < totalSamples; i++)
            {
                float3 firstBasis = normalize(cross(getPoissonSample(i), rayDirection));
                float3 secondBasis = normalize(cross(rayDirection, firstBasis));
                // S24 (2026-09-06, task #156 hotfix round 3): GLSL's
                // vec2(scalar) single-arg constructor broadcasts to both
                // components - HLSL has no such constructor overload, a
                // cast was used as the splat idiom, faithfully matching the
                // GLSL source's own vec2(scalar) call.
                //
                // S24 (2026-09-07, task #266/#315 continuation): that
                // faithful port carried over TWO real, structural bugs live-
                // reported as a "torn"/"smeared"/incoherent-between-pixels
                // reflection that no amount of sample-count or iteration-
                // count tuning could fix (Cat.png/Cat2.png/Cat3.png/
                // floor.png) - and correctly so, since neither bug is in the
                // march itself, both are in the STARTING direction fed into
                // it, upstream of every march-quality fix tried tonight:
                // (1) the splat forces coeffs.x==coeffs.y always, so the
                //     jitter isn't 2D at all - it's locked to a single fixed
                //     diagonal (firstBasis+secondBasis), only its magnitude
                //     varies, not its direction.
                // (2) random() returns [0,1), so a sum of two calls is
                //     [0,2) with a mean around 1.0 - never zero, never
                //     negative. This is not "the true reflection direction,
                //     jittered around it" - it's "the true direction, plus a
                //     guaranteed one-sided push," always in the same fixed
                //     diagonal, with a magnitude that's never zero.
                // Every neighboring pixel gets an independently-biased,
                // never-zero, single-axis-locked push away from the correct
                // mirror direction - exactly the incoherent, torn look
                // reported, and correctly untouched by tuning ray-march
                // quality settings, since those only affect how well the
                // march converges onto whatever (wrong) direction it's
                // given. Fixed: two INDEPENDENT random sums (real 2D
                // jitter, not one axis locked to the other) and both
                // centered by -1.0 so the sum-of-two-uniforms range [0,2)
                // becomes [-1,1) - a genuine, symmetric jitter around the
                // true direction instead of a one-sided shove.
                float2 coeffs = float2(
                    random(tc + float2(0, i)) + random(tc + float2(i, 0)),
                    random(tc + float2(i, i)) + random(tc + float2(-i, -i))
                ) - 1.0;
                // S24 (2026-09-06, task #271): progressive blur - widens the
                // jitter spread for reflections that land far from the
                // reflecting surface itself (using `depth`'s existing
                // carry-over from the previous iteration's hit, vs the fixed
                // startDepth captured above), so multi-sample averaging
                // naturally softens "deep" reflections into suggested
                // shapes while close/shallow ones stay sharp. Dialed back
                // (2026-09-06, live test) - the original 2x-at-8-units cap
                // spread individual stochastic samples too far apart for
                // this sample count to blend smoothly, showing as visible
                // banding/streaking instead of a soft blur. Now caps at
                // 1.3x spread over a longer 14-unit range - gentler curve,
                // less likely to outrun what this many samples can smooth.
                float depthBlur = saturate(abs(depth - startDepth) / 14.0);
                float3 reflectionDirectionRandomized = rayDirection + ((firstBasis * coeffs.x + secondBasis * coeffs.y) * glossiness * (1.0 + depthBlur * 0.3));

                // S24: the GLSL original passes `depth` as BOTH the out
                // hitDepth destination (4th arg) and the by-value depth
                // input (5th arg) of the same call - legal in both GLSL and
                // HLSL since the by-value copy happens at call time, before
                // the out-param writeback on return. This means `depth`
                // progressively gets overwritten by each iteration's hit
                // depth and feeds into the NEXT iteration's call - real,
                // load-bearing behavior of the source algorithm, not
                // incidental, so it's preserved here exactly (not given a
                // separate unused local).
                bool hit = traceScreenRay(viewPos, normalize(reflectionDirectionRandomized), hitpoint, depth, depth, source, sourceSampler);

                // S24 (2026-09-06, task #271 rebuild; loosened 2026-09-07,
                // task #266/#315 continuation): reject hits whose sampled
                // color is suspiciously near-black - a genuinely dark/
                // shadowed reflection still has some non-zero variation; a
                // uniformly pure-black result is the signature of the ray
                // converging on an unpainted/invalid region of the copied
                // scene rather than real geometry. Live-reported (Cat.png)
                // as "reflections are way too scattered" on a genuinely
                // dark wood floor - the original 0.01 threshold is high
                // enough to also catch REAL, VALID dark-but-nonzero hits on
                // a naturally dark surface, discarding them as if they were
                // misses. Since a discarded-but-valid dark hit and a true
                // miss look identical (hits-- either way), that
                // systematically strips out the correct dark majority of
                // this floor's reflection and leaves only the sparse bright
                // hits behind - exactly a "scattered dots on black" look.
                // Tightened an order of magnitude so it only catches
                // genuinely near-zero/invalid regions, not real dark
                // content.
                if (hit && dot(hitpoint.rgb, float3(0.333, 0.334, 0.333)) < 0.0008)
                {
                    hit = false;
                }

                hitpoint.a = 0;

                // S24 (2026-09-07, task #266/#315 continuation): firefly
                // clamp - with only glossySampleCount (default 4) fully
                // independent stochastic samples per pixel and zero
                // temporal accumulation anywhere in this SSR
                // implementation, a single sample landing on a genuinely
                // bright pixel (a lamp, a window, a bright material) reads
                // as a huge outlier relative to its dark neighbors once
                // averaged over so few samples - the classic Monte-Carlo
                // "firefly" artifact, and the exact bright-scattered-dot
                // look reported. Clamping each individual hit's luminance
                // before accumulation (not the final averaged result, which
                // already has its own separate boost/highlight treatment
                // below) caps how much any one lucky/unlucky sample can
                // dominate the average, without discarding it outright the
                // way the near-black rejection above does for the opposite
                // extreme. 4.0 - generous, HDR-appropriate, first-pass
                // value, not physically derived.
                float hitLuminance = dot(hitpoint.rgb, float3(0.2126, 0.7152, 0.0722));
                if (hitLuminance > 4.0)
                {
                    hitpoint.rgb *= 4.0 / hitLuminance;
                }

                if (hit)
                {
                    ++hits;
                    collectedColor += hitpoint;
                    collectedColor.a += 1;
                }
            }

            if (hits > 0)
            {
                collectedColor /= hits;
                // S24 (2026-09-06, task #271): modest color/luminosity
                // boost - live feedback that correct SSR hits still looked
                // noticeably dark/desaturated. Lifts saturation slightly
                // (push away from the color's own luminance) then overall
                // brightness (1.25->1.35, bumped again per live feedback).
                // Tunable, not derived from anything physical.
                float luminance = dot(collectedColor.rgb, float3(0.2126, 0.7152, 0.0722));
                collectedColor.rgb = lerp(float3(luminance, luminance, luminance), collectedColor.rgb, 1.2) * 1.35;
                // S24 (2026-09-06, task #271): light-source boost - bright
                // pixels (lamps, screens, windows) reflected via SSR get an
                // extra lift above a threshold, on top of the flat boost
                // above, so real light sources visibly pop in the
                // reflection rather than just being uniformly brighter like
                // everything else. Smooth quadratic ramp, not a hard cutoff.
                float highlight = saturate((luminance - 0.6) / 0.4);
                collectedColor.rgb += collectedColor.rgb * highlight * highlight * 1.5;
            }
            else
            {
                collectedColor = float4(0, 0, 0, 0);
            }
        }
    }
    float hitAlpha = hits;
    hitAlpha /= totalSamples;
    // S24 (2026-09-06, task #271 rebuild): sqrt()'ing the 3 vignette terms
    // individually wasn't enough on its own - boosting the FINAL combined
    // weight directly here instead, so a real hit reaches much closer to a
    // full override of the cube regardless of how the 3 terms above
    // multiply together. Zero hits still means hitAlpha=0, so this can
    // never manufacture a contribution where there genuinely isn't one -
    // only strengthens a real one. Dialed back from 4x to 2x (2026-09-06,
    // live test) - 4x combined with the near-black-hit guard above still
    // over-amplified toward the cube's replacement; retune from here.
    collectedColor.a = saturate(hitAlpha * vignette * 2.0);
    return (float)hits;
}
