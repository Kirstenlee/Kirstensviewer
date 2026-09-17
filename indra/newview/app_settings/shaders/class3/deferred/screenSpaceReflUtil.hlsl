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

// Only consumer is reflectionProbeF.hlsl's doProbeSample()/
// sampleReflectionProbesLegacy(). The standalone screenSpaceReflPostV/F.hlsl
// program, and screenSpaceReflTraceF/FilterF/AlphaV/F/WaterF.hlsl, are not
// wired to any registered shader program and don't feed this.

Texture2D sceneDepth : register(t18);
// sceneDepth reuses deferredUtil.hlsl's depthMapSampler (s1) rather than
// declaring a new one - D3D11 pixel-shader sampler slots are hard-capped at
// 16 (s0-s15). deferredUtil.hlsl is always co-attached wherever this file is,
// so depthMapSampler is guaranteed present; guarded via its own
// LL_DEPTHMAP_DECLARED macro to avoid an X3003 redefinition.
#ifndef LL_DEPTHMAP_DECLARED
#define LL_DEPTHMAP_DECLARED
uniform SamplerState depthMapSampler : register(s1);
#endif

// inv_proj/screen_res are also declared, as a pair, by deferredUtil.hlsl
// under LL_INV_PROJ_DECLARED - reused here under the same guard to avoid
// an X3003 redefinition.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif
uniform float4x4 projection_matrix;
// last_projection_matrix: same dedicated uniform temporalResolveSSAOF.hlsl
// uses for projecting a position already moved into last-frame view space.
// Uploaded by LLPipeline::bindReflectionProbes() (pipeline.cpp).
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

// Low-level forward-projection primitive: view-space position + an explicit
// projection matrix -> screen UV. Guards w<=0 before the perspective divide
// (unconditional divide produces wild/inverted UVs once w is small or
// negative) and returns sentinel float2(-1,-1) on failure, which fails the
// [0,1] bounds check at every call site. Returns UNFLIPPED UV, matching
// getScreenCoord()/getPositionWithNDC() convention - Y-flip is applied only
// at the actual .Sample() call sites that need it (getDepth()'s pattern).
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

// Projects a CURRENT-frame view-space position using projection_matrix.
// Called externally by pbralphaF.hlsl - only for flip-invariant purposes
// (vignette falloff, jitter seed), never a direct texture sample, so the
// unflipped return convention is safe there.
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
// Hard cap on ray march distance (world units, along marching position's Z).
// Stops the ray before the far, coarsely-stepped zone where hits look
// banded/distorted. Misses beyond this fall back to nothing rather than a
// graceful cube fallback (cube contribution is zeroed for SSR-eligible
// surfaces, see reflectionProbeF.hlsl's doProbeSample()).
static float maxReflectionDepth = 6.0;

// tc is the UNFLIPPED convention (matches ssrProject()'s return and
// getPositionWithDepth()'s expectation) - Y-flip applied only at the
// .Sample() call below, matching deferredUtil.hlsl's getDepth()/getNorm().
float getLinearDepth(float2 tc)
{
    // SampleLevel (explicit LOD 0), not Sample: this runs inside
    // traceScreenRay()'s [loop] whose trip count (iterationCount) is a
    // runtime uniform, so Sample()'s implicit gradient computation would
    // require full unroll and fail (X3570/X3511).
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
        // [loop] forces a real dynamic loop: iterationCount is a runtime
        // uniform, so trip count is never statically known and the compiler's
        // default unroll attempt would fail (X3570/X3511).
        [loop]
        for (; i < (int)iterationCount && !hit; i++)
        {
            if (abs(marchingPosition.z - position.z) > maxReflectionDepth)
            {
                hit = false;
                break;
            }
            // marchingPosition lives in LAST-FRAME view space (transformed
            // once, above), so it must be projected with last_projection_matrix,
            // not the current frame's projection_matrix.
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
                    // sign() always returns int in HLSL, so `sign(delta) / 2`
                    // would be integer division (always 0) - divide by 2.0
                    // instead to force a real float divide.
                    color = float4(0.5 + sign(delta) / 2.0, 0.3, 0.5 - sign(delta) / 2.0, 0);
                // Y-flip applied here at the actual sample call; screenPosition
                // itself stays unflipped (see ssrProject()'s comment).
                hitColor = textureFrame.SampleLevel(textureFrameSampler, float2(screenPosition.x, 1.0 - screenPosition.y), 0) * color;
                hitDepth = depthFromScreen;

                // Distance-based confidence fade: fades hitColor toward black
                // as travel distance approaches maxReflectionDepth instead of
                // a hard cliff, so far/grazing-angle hits (least reliable for
                // screen-space marching) de-weight gracefully toward the cube/
                // probe fallback instead of showing full-confidence wrong color.
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
            // Tracks whether this iteration just overshot the surface, to gate
            // exponential growth below. Declared outside isAdaptiveStepEnabled
            // so it stays in scope (0, never suppressing growth) when that's off.
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

            // Gated on overshotSign<=0: once the march has overshot and entered
            // convergence mode (adaptive branch shrinking the step), exponential
            // growth must not run and undo that correction - it would re-accelerate
            // past the true crossing instead of converging onto it.
            if (isExponentialStepEnabled && overshotSign <= 0.0)
            {
                step *= adaptiveStepMultiplier;

                // Step magnitude capped to a fraction of maxReflectionDepth:
                // uncapped exponential growth can jump a huge fraction of the
                // whole march budget in one step by later iterations, sending
                // independent stochastic samples to wildly different surfaces.
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

                // Same as the main loop above: project with last_projection_matrix.
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
                        // Same int-division sign() fix as the main loop above.
                    color = float4(0.5 + sign(delta) / 2.0, 0.3, 0.5 - sign(delta) / 2.0, 0);
                    // Same flip-at-sample fix as the main loop's hit above.
                    hitColor = textureFrame.SampleLevel(textureFrameSampler, float2(screenPosition.x, 1.0 - screenPosition.y), 0) * color;
                    hitDepth = depthFromScreen;

                    // Same distance-based confidence fade as the coarse loop above.
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

// 128-entry Poisson-disc sample table, ported verbatim from the GLSL source.
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
    // startDepth stays fixed at this surface point's distance from the camera;
    // `depth` below gets overwritten with each iteration's own hit depth
    // (see the traceScreenRay() call further down), and startDepth is needed
    // to measure how far a found reflection is FROM the surface for the
    // progressive-blur jitter widening below.
    float startDepth = depth;

    float3 rayDirection = normalize(reflect(viewPos, normalize(n)));

    // The GLSL original also computes a `jitter` value here that is never
    // read anywhere in the function - omitted rather than carried over dead.
    //
    // The 3 geometric falloff terms below (screen-position, viewing angle,
    // far-distance) are sqrt()'d - keeps 0->0 and 1->1 but lifts mid-range
    // values so a decent-but-not-perfect angle/position isn't crushed as hard.
    // A 4th hardcoded glossiness-gate term from the original is removed:
    // redundant with (and stricter than) reflectionProbeF.hlsl's exposed,
    // user-tunable ssrGlossThreshold, which every caller already clears.
    float2 screenpos = 1 - abs(tc * 2 - 1);
    float vignette = sqrt(clamp((abs(screenpos.x) * abs(screenpos.y)) * 16, 0, 1));
    vignette *= sqrt(clamp((dot(normalize(viewPos), n) * 0.5 + 0.5) * 5.5 - 0.8, 0, 1));

    float zFar = 128.0;
    vignette *= sqrt(clamp(1.0 + (viewPos.z / zFar), 0.0, 1.0));

    float4 hitpoint;

    glossiness = 1 - glossiness;

    // S24: was `max(glossySampleCount, glossySampleCount * glossiness * vignette)` - since
    // glossiness/vignette are both clamped [0,1], the second term can never exceed the first,
    // so that max() always just returned glossySampleCount unconditionally, silently disabling
    // the intended adaptive scale-down (fewer samples for sharper/lower-confidence reflections,
    // full budget for rough surfaces that need denoising and full-confidence pixels).
    totalSamples = (int)(glossySampleCount * glossiness * vignette);

    totalSamples = max(totalSamples, 1);
    if (glossiness < 0.35)
    {
        if (vignette > 0)
        {
            for (int i = 0; i < totalSamples; i++)
            {
                float3 firstBasis = normalize(cross(getPoissonSample(i), rayDirection));
                float3 secondBasis = normalize(cross(rayDirection, firstBasis));
                // Two INDEPENDENT random sums, each centered by -1.0 so the
                // sum-of-two-uniforms range [0,2) becomes [-1,1): gives a
                // genuine symmetric 2D jitter around the true reflection
                // direction. A single random(tc+i)*2-1-style splat (HLSL has
                // no vec2(scalar)-broadcast constructor) would lock
                // coeffs.x==coeffs.y, confining the jitter to one fixed
                // diagonal instead of a real 2D spread.
                float2 coeffs = float2(
                    random(tc + float2(0, i)) + random(tc + float2(i, 0)),
                    random(tc + float2(i, i)) + random(tc + float2(-i, -i))
                ) - 1.0;
                // Progressive blur: widens jitter spread for reflections that
                // land far from the reflecting surface (using `depth`'s
                // carry-over from the previous iteration's hit vs. the fixed
                // startDepth), so multi-sample averaging softens "deep"
                // reflections while close/shallow ones stay sharp.
                float depthBlur = saturate(abs(depth - startDepth) / 14.0);
                float3 reflectionDirectionRandomized = rayDirection + ((firstBasis * coeffs.x + secondBasis * coeffs.y) * glossiness * (1.0 + depthBlur * 0.3));

                // `depth` is passed as BOTH the out hitDepth destination
                // (4th arg) and the by-value depth input (5th arg) - legal
                // since the by-value copy happens at call time, before the
                // out-param writeback. `depth` progressively gets overwritten
                // by each iteration's hit depth and feeds the NEXT iteration.
                bool hit = traceScreenRay(viewPos, normalize(reflectionDirectionRandomized), hitpoint, depth, depth, source, sourceSampler);

                // Reject hits whose sampled color is near-black: the signature
                // of the ray converging on an unpainted/invalid region of the
                // copied scene rather than real geometry (a real dark/shadowed
                // hit still has some non-zero variation).
                if (hit && dot(hitpoint.rgb, float3(0.333, 0.334, 0.333)) < 0.0008)
                {
                    hit = false;
                }

                hitpoint.a = 0;

                // Firefly clamp: with only glossySampleCount (default 4)
                // independent stochastic samples per pixel and no temporal
                // accumulation, one sample landing on a bright pixel (lamp,
                // window) is a huge outlier once averaged over so few samples.
                // Clamping each hit's luminance before accumulation caps how
                // much one sample can dominate. 4.0 is a generous, HDR-appropriate,
                // not physically-derived value.
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
                // Tunable color/luminosity boost (not physically derived):
                // lifts saturation slightly then overall brightness, since raw
                // SSR hits otherwise look dark/desaturated.
                float luminance = dot(collectedColor.rgb, float3(0.2126, 0.7152, 0.0722));
                collectedColor.rgb = lerp(float3(luminance, luminance, luminance), collectedColor.rgb, 1.2) * 1.35;
                // Light-source boost: bright pixels (lamps, screens, windows)
                // get an extra lift above the flat boost above via a smooth
                // quadratic ramp, so they visibly pop in the reflection.
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
    // Final combined weight boosted 2x so a real hit reaches closer to a full
    // override of the cube probe. hitAlpha=0 on zero hits, so this only
    // strengthens a real hit, never manufactures one.
    collectedColor.a = saturate(hitAlpha * vignette * 2.0);
    return (float)hits;
}
