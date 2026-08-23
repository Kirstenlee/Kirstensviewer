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
uniform float4x4 modelview_delta;    // transform from last camera space to current camera space
uniform float4x4 inv_modelview_delta;

float4 getPositionWithDepth(float2 pos_screen, float depth);

float random(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453123); //simple random function
}

// Based off of https://github.com/RoundedGlint585/ScreenSpaceReflection/
// A few tweaks here and there to suit our needs.

float2 generateProjectedPosition(float3 pos)
{
    float4 samplePosition = mul(projection_matrix, float4(pos, 1.f));
    samplePosition.xy = (samplePosition.xy / samplePosition.w) * 0.5 + 0.5;
    // S24: flipped once here, at the point of definition, rather than at
    // each .Sample() call site - every use of this return value is either
    // a texture sample (getLinearDepth() below, the final hit-color read
    // in tapScreenSpaceReflection()) or the x/y in-[0,1] bounds-reject
    // check (flip-invariant, since 1-y maps [0,1] to [0,1] too) - unlike
    // vary_fragcoord elsewhere, this value is never used for NDC/position
    // reconstruction, so flipping it once here is safe (matches this
    // session's own established "safe to flip at the point of definition
    // when only used for sampling" rule).
    samplePosition.y = 1.0 - samplePosition.y;
    return samplePosition.xy;
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
    float depth = sceneDepth.SampleLevel(depthMapSampler, tc, 0).r;

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
            screenPosition = generateProjectedPosition(marchingPosition);
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
                    color = float4(0.5 + sign(delta) / 2, 0.3, 0.5 - sign(delta) / 2, 0);
                hitColor = textureFrame.SampleLevel(textureFrameSampler, screenPosition, 0) * color;
                hitDepth = depthFromScreen;
                hit = true;
                break;
            }
            if (isBinarySearchEnabled && delta > 0)
            {
                break;
            }
            if (isAdaptiveStepEnabled)
            {
                float directionSign = sign(abs(marchingPosition.z) - depthFromScreen);
                //this is sort of adapting step, should prevent lining reflection by doing sort of iterative converging
                //some implementation doing it by binary search, but I found this idea more cheaty and way easier to implement
                step = step * (1.0 - rayStep * max(directionSign, 0.0));
                marchingPosition += step * (-directionSign);
            }
            else
            {
                marchingPosition += step;
            }

            if (isExponentialStepEnabled)
            {
                step *= adaptiveStepMultiplier;
            }
        }
        if (isBinarySearchEnabled)
        {
            [loop]
            for (; i < (int)iterationCount && !hit; i++)
            {
                step *= 0.5;
                marchingPosition = marchingPosition - step * sign(delta);

                screenPosition = generateProjectedPosition(marchingPosition);
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
                        color = float4(0.5 + sign(delta) / 2, 0.3, 0.5 - sign(delta) / 2, 0);
                    hitColor = textureFrame.SampleLevel(textureFrameSampler, screenPosition, 0) * color;
                    hitDepth = depthFromScreen;
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

    float3 rayDirection = normalize(reflect(viewPos, normalize(n)));

    // S24: the GLSL original also computes a `jitter` value here
    // (uv2/c/jitter, based on tc*screen_res) that is never actually read
    // anywhere else in the function - confirmed dead in the source being
    // ported, not just this port. Omitted rather than carried over as
    // inert code (also sidesteps GLSL mod() vs HLSL fmod() sign-behavior
    // differences for a value that was never used).
    float2 screenpos = 1 - abs(tc * 2 - 1);
    float vignette = clamp((abs(screenpos.x) * abs(screenpos.y)) * 16, 0, 1);
    vignette *= clamp((dot(normalize(viewPos), n) * 0.5 + 0.5) * 5.5 - 0.8, 0, 1);

    float zFar = 128.0;
    vignette *= clamp(1.0 + (viewPos.z / zFar), 0.0, 1.0);

    vignette *= clamp(glossiness * 3 - 1.7, 0, 1);

    float4 hitpoint;

    glossiness = 1 - glossiness;

    totalSamples = (int)max(glossySampleCount, glossySampleCount * glossiness * vignette);

    totalSamples = max(totalSamples, 1);
    if (glossiness < 0.35)
    {
        if (vignette > 0)
        {
            for (int i = 0; i < totalSamples; i++)
            {
                float3 firstBasis = normalize(cross(getPoissonSample(i), rayDirection));
                float3 secondBasis = normalize(cross(rayDirection, firstBasis));
                // S24: GLSL's vec2(scalar) single-arg constructor broadcasts
                // to both components - HLSL has no such constructor overload
                // (X3014 "incorrect number of arguments" - confirmed via
                // build, task #156 hotfix round 3), a cast is the real HLSL
                // splat idiom instead. Keeps this a single evaluation (not
                // two), matching the GLSL source exactly rather than
                // doubling the random() calls.
                float2 coeffs = (float2)(random(tc + float2(0, i)) + random(tc + float2(i, 0)));
                float3 reflectionDirectionRandomized = rayDirection + ((firstBasis * coeffs.x + secondBasis * coeffs.y) * glossiness);

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

                hitpoint.a = 0;

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
            }
            else
            {
                collectedColor = float4(0, 0, 0, 0);
            }
        }
    }
    float hitAlpha = hits;
    hitAlpha /= totalSamples;
    collectedColor.a = hitAlpha * vignette;
    return (float)hits;
}
