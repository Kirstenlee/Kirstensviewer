/**
 * @file class1/deferred/SMAA.hlsl
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

/*[EXTRA_CODE_HERE]*/

// S24 (2026-08-19, task #227 lead-in): this file was previously a 50-line
// stub of bare forward declarations with NO function bodies at all - none of
// SMAAEdgeDetectV.hlsl/SMAAEdgeDetectF.hlsl/SMAABlendWeightsV.hlsl/
// SMAABlendWeightsF.hlsl/SMAANeighborhoodBlendV.hlsl/SMAANeighborhoodBlendF.hlsl
// (which all #include this file and call into it) could ever link -
// D3DCompile failed every launch with "function 'SMAAEdgeDetectionVS'/
// 'SMAAColorEdgeDetectionPS' missing implementation", so SMAA silently
// disabled itself at every startup (LLViewerShaderMgr::loadShadersDeferred).
// This is the real, faithful port of the algorithm from the ~1475-line
// SMAA.glsl (Jorge Jimenez et al.'s reference SMAA library), scoped to
// exactly what this project's llviewershadermgr.cpp actually wires up:
//   - Color edge detection only (SMAAColorEdgeDetectionPS) - the Luma and
//     Depth edge-detection variants are real functions in SMAA.glsl but
//     have no HLSL entry-point file calling them anywhere in this tree, so
//     they're dead code under both backends here and are not ported.
//   - SMAA_PREDICATION=0 and SMAA_REPROJECTION=0 always (see
//     llviewershadermgr.cpp's `defines` map for the SMAA program group) -
//     matches the #if guards already present, unmodified, in the existing
//     entry-point files, so the predication/velocity-texture code paths are
//     never compiled in either backend and are not ported.
//   - Diagonal + corner detection ARE ported (guarded by the same
//     SMAA_DISABLE_DIAG_DETECTION/SMAA_DISABLE_CORNER_DETECTION defines the
//     Low/Medium presets set) since the High/Ultra presets use them.
//   - SMAAResolvePS/SMAASeparatePS (temporal supersampling / MSAA-separate,
//     optional passes) are not wired up by any C++ call site under either
//     backend and are not ported.
//
// GLSL-vs-HLSL note: the real SMAA.glsl already writes its function bodies
// using float2/float3/float4/int2/mad/lerp/saturate names throughout (via
// its own SMAA_GLSL_* porting-macro layer aliasing them to GLSL's native
// vec2/mix/clamp etc.) - meaning the bodies below are near-verbatim copies
// of the GLSL source, not a syntax rewrite. The one real semantic port is
// the *sampling* macros just below: GLSL's SMAA_HLSL_4 target (which this
// follows) implements SMAASampleLevelZeroOffset() et al as PREPROCESSOR
// MACROS, not functions - this is not a style choice, it's required by
// Direct3D: Texture2D::Sample()/SampleLevel()'s `offset` parameter must be a
// true compile-time immediate literal at the intrinsic call site (SM5
// restriction), not a value merely known-constant through a function
// parameter. Every offset used by this algorithm is a small int2 literal at
// each call site (e.g. int2(-1,0)), so macro-expansion (textually inlining
// the literal right next to the intrinsic) satisfies this; a generic helper
// *function* taking `int2 offset` as a parameter would not compile. Also:
// unlike the entry-point files' 5 top-level functions (whose signatures are
// fixed by the already-existing, unmodified entry files and take explicit
// Texture2D+SamplerState pairs matching this project's usual convention),
// every INTERNAL-only helper below samples through its own fixed
// LinearSampler/PointSampler (declared here, matching GLSL's SMAA_HLSL_4
// porting layer exactly) rather than a caller-supplied sampler - this is
// deliberate and matches upstream: the algorithm's correctness depends on
// point-vs-linear filtering being exactly what each tap needs, not on
// whatever filter mode happens to be bound at the LLTexUnit level for that
// channel. The *Sampler parameters the entry files pass in are therefore
// unused inside the corresponding top-level function bodies below - that's
// intentional, not a leftover.
//
// SMAA_FLIP_Y: GLSL/GL targets set this to 1 (bottom-up texture origin);
// SMAA_HLSL_4/4_1 defaults to 0 (top-left origin, Direct3D's native
// convention) - this project targets D3D11 throughout, so the API_V_*
// macros below are hardcoded to the FLIP_Y=0 branch directly rather than
// keeping GLSL's runtime #if, matching the same GL-vs-D3D UV-origin
// direction already resolved (the other way, i.e. GL needing the flip) in
// every other file this project has hit this in (SSAO dither, avatar
// impostor UVs, hero-probe viewport).

#ifdef VERTEX_SHADER
    #define SMAA_INCLUDE_VS 1
    #define SMAA_INCLUDE_PS 0
#else
    #define SMAA_INCLUDE_VS 0
    #define SMAA_INCLUDE_PS 1
#endif

uniform float4 SMAA_RT_METRICS;

//-----------------------------------------------------------------------------
// SMAA Presets
//
// If you use one of these presets, the following configuration macros are
// ignored if also set in the "Configurable Defines" section below - matches
// SMAA.glsl exactly (llviewershadermgr.cpp defines exactly one of these per
// quality permutation: SMAA_PRESET_LOW/MEDIUM/HIGH/ULTRA).

#if defined(SMAA_PRESET_LOW)
#define SMAA_THRESHOLD 0.15
#define SMAA_MAX_SEARCH_STEPS 4
#define SMAA_DISABLE_DIAG_DETECTION
#define SMAA_DISABLE_CORNER_DETECTION
#elif defined(SMAA_PRESET_MEDIUM)
#define SMAA_THRESHOLD 0.1
#define SMAA_MAX_SEARCH_STEPS 8
#define SMAA_DISABLE_DIAG_DETECTION
#define SMAA_DISABLE_CORNER_DETECTION
#elif defined(SMAA_PRESET_HIGH)
#define SMAA_THRESHOLD 0.1
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_CORNER_ROUNDING 25
#elif defined(SMAA_PRESET_ULTRA)
#define SMAA_THRESHOLD 0.05
#define SMAA_MAX_SEARCH_STEPS 32
#define SMAA_MAX_SEARCH_STEPS_DIAG 16
#define SMAA_CORNER_ROUNDING 25
#endif

//-----------------------------------------------------------------------------
// Configurable Defines

#ifndef SMAA_THRESHOLD
#define SMAA_THRESHOLD 0.1
#endif

#ifndef SMAA_DEPTH_THRESHOLD
#define SMAA_DEPTH_THRESHOLD (0.1 * SMAA_THRESHOLD)
#endif

#ifndef SMAA_MAX_SEARCH_STEPS
#define SMAA_MAX_SEARCH_STEPS 16
#endif

#ifndef SMAA_MAX_SEARCH_STEPS_DIAG
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#endif

#ifndef SMAA_CORNER_ROUNDING
#define SMAA_CORNER_ROUNDING 25
#endif

#ifndef SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR
#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0
#endif

#ifndef SMAA_PREDICATION
#define SMAA_PREDICATION 0
#endif

#ifndef SMAA_PREDICATION_THRESHOLD
#define SMAA_PREDICATION_THRESHOLD 0.01
#endif

#ifndef SMAA_PREDICATION_SCALE
#define SMAA_PREDICATION_SCALE 2.0
#endif

#ifndef SMAA_PREDICATION_STRENGTH
#define SMAA_PREDICATION_STRENGTH 0.4
#endif

#ifndef SMAA_REPROJECTION
#define SMAA_REPROJECTION 0
#endif

#ifndef SMAA_REPROJECTION_WEIGHT_SCALE
#define SMAA_REPROJECTION_WEIGHT_SCALE 30.0
#endif

//-----------------------------------------------------------------------------
// Texture Access Defines

#ifndef SMAA_AREATEX_SELECT
#define SMAA_AREATEX_SELECT(sample) sample.rg
#endif

#ifndef SMAA_SEARCHTEX_SELECT
#define SMAA_SEARCHTEX_SELECT(sample) sample.r
#endif

//-----------------------------------------------------------------------------
// Non-Configurable Defines

#define SMAA_AREATEX_MAX_DISTANCE 16
#define SMAA_AREATEX_MAX_DISTANCE_DIAG 20
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / float2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE float2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE float2(64.0, 16.0)
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)

//-----------------------------------------------------------------------------
// Porting: sampler objects + sampling macros (SMAA_HLSL_4 target, see
// top-of-file comment for why these must stay macros, not functions)

// S24 (2026-09-04): explicit high slots (s13/s14), matching this project's
// established "safe unused register" convention (see reflectionProbeF.hlsl's
// t16/t17 comment) - every SMAA entry-point file's own per-texture samplers
// (edgesTexSampler/areaTexSampler/searchTexSampler etc, all declared/unused
// per the comments below) sit at s0-s2, so s13/s14 can never collide. This
// codebase's raw-D3D11 shader path does NOT auto-bind samplers from an
// inline initializer block like this - unlike the Effects11 framework, the
// {Filter=...} values here are compiled-in metadata only, not a real bound
// sampler. Without an explicit register (letting the compiler auto-assign
// one) AND a matching PSSetSamplers() call at that slot from C++, these reads
// used whatever sampler a prior, unrelated draw left bound to the auto-
// assigned slot that frame - undefined per-frame garbage, not really
// point/linear at all. DXSampler::bindStatic(13,...)/(14,...) in
// generateSMAABuffers()/applySMAA() (pipeline.cpp) now binds real samplers
// here every pass.
SamplerState LinearSampler : register(s13) { Filter = MIN_MAG_LINEAR_MIP_POINT; AddressU = Clamp; AddressV = Clamp; };
SamplerState PointSampler : register(s14) { Filter = MIN_MAG_MIP_POINT; AddressU = Clamp; AddressV = Clamp; };

#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(LinearSampler, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(PointSampler, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(LinearSampler, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]

// S24 (2026-08-19, live-test fix): first attempt hardcoded the SMAA_FLIP_Y=0
// ("vanilla D3D11 top-left origin") branch per the reference library's own
// stated SMAA_HLSL_4 default - live-tested WRONG, entire frame rendered
// upside down. Same GL-vs-D3D origin-convention bug class this project has
// hit repeatedly (SSAO dither, avatar impostor UVs, hero-probe viewport) -
// this pipeline's intermediate render targets evidently need the SAME
// bottom-up-origin compensation GLSL's own SMAA_GLSL_4 branch uses, not the
// reference library's generic HLSL default. Swapped to match.
#define API_V_DIR(v) (-(v))
#define API_V_COORD(v) (1.0 - (v))
#define API_V_BELOW(v1, v2) ((v1) < (v2))
#define API_V_ABOVE(v1, v2) ((v1) > (v2))

//-----------------------------------------------------------------------------
// Misc functions

void SMAAMovc(bool2 cond, inout float2 variable, float2 value) {
    SMAA_FLATTEN if (cond.x) variable.x = value.x;
    SMAA_FLATTEN if (cond.y) variable.y = value.y;
}

void SMAAMovc(bool4 cond, inout float4 variable, float4 value) {
    SMAAMovc(cond.xy, variable.xy, value.xy);
    SMAAMovc(cond.zw, variable.zw, value.zw);
}

#if SMAA_INCLUDE_VS
//-----------------------------------------------------------------------------
// Vertex Shaders

void SMAAEdgeDetectionVS(float2 texcoord,
                         out float4 offset[3]) {
    offset[0] = mad(SMAA_RT_METRICS.xyxy, float4(-1.0, 0.0, 0.0, API_V_DIR(-1.0)), texcoord.xyxy);
    offset[1] = mad(SMAA_RT_METRICS.xyxy, float4( 1.0, 0.0, 0.0, API_V_DIR(1.0)), texcoord.xyxy);
    offset[2] = mad(SMAA_RT_METRICS.xyxy, float4(-2.0, 0.0, 0.0, API_V_DIR(-2.0)), texcoord.xyxy);
}

void SMAABlendingWeightCalculationVS(float2 texcoord,
                                     out float2 pixcoord,
                                     out float4 offset[3]) {
    pixcoord = texcoord * SMAA_RT_METRICS.zw;

    // We will use these offsets for the searches later on (see @PSEUDO_GATHER4):
    offset[0] = mad(SMAA_RT_METRICS.xyxy, float4(-0.25, API_V_DIR(-0.125),  1.25, API_V_DIR(-0.125)), texcoord.xyxy);
    offset[1] = mad(SMAA_RT_METRICS.xyxy, float4(-0.125, API_V_DIR(-0.25), -0.125,  API_V_DIR(1.25)), texcoord.xyxy);

    // And these for the searches, they indicate the ends of the loops:
    offset[2] = mad(SMAA_RT_METRICS.xxyy,
                    float4(-2.0, 2.0, API_V_DIR(-2.0), API_V_DIR(2.0)) * float(SMAA_MAX_SEARCH_STEPS),
                    float4(offset[0].xz, offset[1].yw));
}

void SMAANeighborhoodBlendingVS(float2 texcoord,
                                out float4 offset) {
    offset = mad(SMAA_RT_METRICS.xyxy, float4( 1.0, 0.0, 0.0, API_V_DIR(1.0)), texcoord.xyxy);
}
#endif // SMAA_INCLUDE_VS

#if SMAA_INCLUDE_PS
//-----------------------------------------------------------------------------
// Edge Detection Pixel Shader (First Pass) - color variant only, see
// top-of-file comment (luma/depth variants are unreachable dead code here).

float2 SMAAColorEdgeDetectionPS(float2 texcoord,
                                float4 offset[3],
                                Texture2D colorTex, SamplerState colorTexSampler
                                #if SMAA_PREDICATION
                                , Texture2D predicationTex, SamplerState predicationTexSampler
                                #endif
                                ) {
    // SMAA_PREDICATION is always 0 in this project (see top-of-file comment) -
    // colorTexSampler/predicationTex* are unused; sampling below always goes
    // through this file's own LinearSampler/PointSampler.

    // S24 (2026-08-19, live-test fix): colorTex here is the raw scene buffer
    // (src/sourceBuffer passed to generateSMAABuffers()) - same GL-style
    // bottom-up storage glowcombineFXAAF.hlsl already compensates for when
    // reading the identical buffer at the identical pipeline stage
    // (float2(tc.x, 1.0-tc.y)) for FXAA's own bake step. Missing this exact
    // flip here was why the whole frame rendered upside down with SMAA on -
    // flipped once, before all the neighbor sampling below, matching
    // fxaaF.hlsl's "flip the base coordinate once" approach (safe because
    // every sample below is colorTex - unlike SMAANeighborhoodBlendingPS,
    // which also reads SMAA's own self-consistent blendTex and must NOT
    // flip that one).
    texcoord.y = 1.0 - texcoord.y;
    offset[0].y = 1.0 - offset[0].y;
    offset[0].w = 1.0 - offset[0].w;
    offset[1].y = 1.0 - offset[1].y;
    offset[1].w = 1.0 - offset[1].w;
    offset[2].y = 1.0 - offset[2].y;
    offset[2].w = 1.0 - offset[2].w;

    float2 threshold = float2(SMAA_THRESHOLD, SMAA_THRESHOLD);

    // Calculate color deltas:
    float4 delta;
    float3 C = SMAASamplePoint(colorTex, texcoord).rgb;

    float3 Cleft = SMAASamplePoint(colorTex, offset[0].xy).rgb;
    float3 t = abs(C - Cleft);
    delta.x = max(max(t.r, t.g), t.b);

    float3 Ctop  = SMAASamplePoint(colorTex, offset[0].zw).rgb;
    t = abs(C - Ctop);
    delta.y = max(max(t.r, t.g), t.b);

    // We do the usual threshold:
    float2 edges = step(threshold, delta.xy);

    // Then discard if there is no edge:
    if (dot(edges, float2(1.0, 1.0)) == 0.0)
        discard;

    // Calculate right and bottom deltas:
    float3 Cright = SMAASamplePoint(colorTex, offset[1].xy).rgb;
    t = abs(C - Cright);
    delta.z = max(max(t.r, t.g), t.b);

    float3 Cbottom  = SMAASamplePoint(colorTex, offset[1].zw).rgb;
    t = abs(C - Cbottom);
    delta.w = max(max(t.r, t.g), t.b);

    // Calculate the maximum delta in the direct neighborhood:
    float2 maxDelta = max(delta.xy, delta.zw);

    // Calculate left-left and top-top deltas:
    float3 Cleftleft  = SMAASamplePoint(colorTex, offset[2].xy).rgb;
    t = abs(C - Cleftleft);
    delta.z = max(max(t.r, t.g), t.b);

    float3 Ctoptop = SMAASamplePoint(colorTex, offset[2].zw).rgb;
    t = abs(C - Ctoptop);
    delta.w = max(max(t.r, t.g), t.b);

    // Calculate the final maximum delta:
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation:
    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    return edges;
}

//-----------------------------------------------------------------------------
// Diagonal Search Functions

#if !defined(SMAA_DISABLE_DIAG_DETECTION)

float2 SMAADecodeDiagBilinearAccess(float2 e) {
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return round(e);
}

float4 SMAADecodeDiagBilinearAccess(float4 e) {
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return round(e);
}

float2 SMAASearchDiag1(Texture2D edgesTex, float2 texcoord, float2 dir, out float2 e) {
    dir.y = API_V_DIR(dir.y);
    float4 coord = float4(texcoord, -1.0, 1.0);
    float3 t = float3(SMAA_RT_METRICS.xy, 1.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) &&
           coord.w > 0.9) {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);
        e = SMAASampleLevelZero(edgesTex, coord.xy).rg;
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAASearchDiag2(Texture2D edgesTex, float2 texcoord, float2 dir, out float2 e) {
    dir.y = API_V_DIR(dir.y);
    float4 coord = float4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * SMAA_RT_METRICS.x; // See @SearchDiag2Optimization
    float3 t = float3(SMAA_RT_METRICS.xy, 1.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) &&
           coord.w > 0.9) {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);

        // @SearchDiag2Optimization - fetch both edges at once via bilinear:
        e = SMAASampleLevelZero(edgesTex, coord.xy).rg;
        e = SMAADecodeDiagBilinearAccess(e);

        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAAAreaDiag(Texture2D areaTex, float2 dist, float2 e, float offset) {
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);

    // Scale and bias for mapping to texel space:
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    // Diagonal areas are on the second half of the texture:
    texcoord.x += 0.5;

    // Move to proper place, according to the subpixel offset:
    texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;

    texcoord.y = API_V_COORD(texcoord.y);

    return SMAA_AREATEX_SELECT(SMAASampleLevelZero(areaTex, texcoord));
}

float2 SMAACalculateDiagWeights(Texture2D edgesTex, Texture2D areaTex, float2 texcoord, float2 e, float4 subsampleIndices) {
    float2 weights = float2(0.0, 0.0);

    // Search for the line ends:
    float4 d;
    float2 end;
    if (e.r > 0.0) {
        d.xz = SMAASearchDiag1(edgesTex, texcoord, float2(-1.0,  1.0), end);
        d.x += float(end.y > 0.9);
    } else
        d.xz = float2(0.0, 0.0);
    d.yw = SMAASearchDiag1(edgesTex, texcoord, float2(1.0, -1.0), end);

    SMAA_BRANCH
    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        // Fetch the crossing edges:
        float4 coords = mad(float4(-d.x + 0.25, API_V_DIR(d.x), d.y, API_V_DIR(-d.y - 0.25)), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
        float4 c;
        c.xy = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2(-1,  0)).rg;
        c.zw = SMAASampleLevelZeroOffset(edgesTex, coords.zw, int2( 1,  0)).rg;
        c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);

        // Merge crossing edges at each side into a single value:
        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

        // Remove the crossing edge if we didn't found the end of the line:
        SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

        // Fetch the areas for this line:
        weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.z);
    }

    // Search for the line ends:
    d.xz = SMAASearchDiag2(edgesTex, texcoord, float2(-1.0, -1.0), end);
    if (SMAASampleLevelZeroOffset(edgesTex, texcoord, int2(1, 0)).r > 0.0) {
        d.yw = SMAASearchDiag2(edgesTex, texcoord, float2(1.0, 1.0), end);
        d.y += float(end.y > 0.9);
    } else
        d.yw = float2(0.0, 0.0);

    SMAA_BRANCH
    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        // Fetch the crossing edges:
        float4 coords = mad(float4(-d.x, API_V_DIR(-d.x), d.y, API_V_DIR(d.y)), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
        float4 c;
        c.x  = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2(-1,  0)).g;
        c.y  = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2( 0, API_V_DIR(-1))).r;
        c.zw = SMAASampleLevelZeroOffset(edgesTex, coords.zw, int2( 1,  0)).gr;
        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

        // Remove the crossing edge if we didn't found the end of the line:
        SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

        // Fetch the areas for this line:
        weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.w).gr;
    }

    return weights;
}
#endif // !SMAA_DISABLE_DIAG_DETECTION

//-----------------------------------------------------------------------------
// Horizontal/Vertical Search Functions

float SMAASearchLength(Texture2D searchTex, float2 e, float offset) {
    // The texture is flipped vertically, with left and right cases taking half
    // of the space horizontally:
    float2 scale = SMAA_SEARCHTEX_SIZE * float2(0.5, -1.0);
    float2 bias = SMAA_SEARCHTEX_SIZE * float2(offset, 1.0);

    // Scale and bias to access texel centers:
    scale += float2(-1.0,  1.0);
    bias  += float2( 0.5, -0.5);

    // Convert from pixel coordinates to texcoords:
    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

    float2 coord = mad(scale, e, bias);
    coord.y = API_V_COORD(coord.y);

    return SMAA_SEARCHTEX_SELECT(SMAASampleLevelZero(searchTex, coord));
}

float SMAASearchXLeft(Texture2D edgesTex, Texture2D searchTex, float2 texcoord, float end) {
    float2 e = float2(0.0, 1.0);
    while (texcoord.x > end &&
           e.g > 0.8281 && // Is there some edge not activated?
           e.r == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(edgesTex, texcoord).rg;
        texcoord = mad(-float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }

    float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchXRight(Texture2D edgesTex, Texture2D searchTex, float2 texcoord, float end) {
    float2 e = float2(0.0, 1.0);
    while (texcoord.x < end &&
           e.g > 0.8281 && // Is there some edge not activated?
           e.r == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(edgesTex, texcoord).rg;
        texcoord = mad(float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchYUp(Texture2D edgesTex, Texture2D searchTex, float2 texcoord, float end) {
    float2 e = float2(1.0, 0.0);
    while (API_V_BELOW(texcoord.y, end) &&
           e.r > 0.8281 && // Is there some edge not activated?
           e.g == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(edgesTex, texcoord).rg;
        texcoord = mad(-float2(0.0, API_V_DIR(2.0)), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e.gr, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.y, API_V_DIR(offset), texcoord.y);
}

float SMAASearchYDown(Texture2D edgesTex, Texture2D searchTex, float2 texcoord, float end) {
    float2 e = float2(1.0, 0.0);
    while (API_V_ABOVE(texcoord.y, end) &&
           e.r > 0.8281 && // Is there some edge not activated?
           e.g == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(edgesTex, texcoord).rg;
        texcoord = mad(float2(0.0, API_V_DIR(2.0)), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e.gr, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.y, API_V_DIR(offset), texcoord.y);
}

float2 SMAAArea(Texture2D areaTex, float2 dist, float e1, float e2, float offset) {
    // Rounding prevents precision errors of bilinear filtering:
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE), round(4.0 * float2(e1, e2)), dist);

    // Scale and bias for mapping to texel space:
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    // Move to proper place, according to the subpixel offset:
    texcoord.y = mad(SMAA_AREATEX_SUBTEX_SIZE, offset, texcoord.y);

    texcoord.y = API_V_COORD(texcoord.y);

    return SMAA_AREATEX_SELECT(SMAASampleLevelZero(areaTex, texcoord));
}

//-----------------------------------------------------------------------------
// Corner Detection Functions

void SMAADetectHorizontalCornerPattern(Texture2D edgesTex, inout float2 weights, float4 texcoord, float2 d) {
    #if !defined(SMAA_DISABLE_CORNER_DETECTION)
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y; // Reduce blending for pixels in the center of a line.

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(0,  API_V_DIR(1))).r;
    factor.x -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(1,  API_V_DIR(1))).r;
    factor.y -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(0, API_V_DIR(-2))).r;
    factor.y -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(1, API_V_DIR(-2))).r;

    weights *= saturate(factor);
    #endif
}

void SMAADetectVerticalCornerPattern(Texture2D edgesTex, inout float2 weights, float4 texcoord, float2 d) {
    #if !defined(SMAA_DISABLE_CORNER_DETECTION)
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2( 1, 0)).g;
    factor.x -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2( 1, API_V_DIR(1))).g;
    factor.y -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(-2, 0)).g;
    factor.y -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(-2, API_V_DIR(1))).g;

    weights *= saturate(factor);
    #endif
}

//-----------------------------------------------------------------------------
// Blending Weight Calculation Pixel Shader (Second Pass)

float4 SMAABlendingWeightCalculationPS(float2 texcoord,
                                       float2 pixcoord,
                                       float4 offset[3],
                                       Texture2D edgesTex, SamplerState edgesTexSampler,
                                       Texture2D areaTex, SamplerState areaTexSampler,
                                       Texture2D searchTex, SamplerState searchTexSampler,
                                       float4 subsampleIndices) { // Just pass zero for SMAA 1x.
    // edgesTexSampler/areaTexSampler/searchTexSampler are unused - see
    // top-of-file comment (sampling goes through this file's own
    // LinearSampler/PointSampler for correctness, matching upstream).
    float4 weights = float4(0.0, 0.0, 0.0, 0.0);

    float2 e = SMAASample(edgesTex, texcoord).rg;

    SMAA_BRANCH
    if (e.g > 0.0) { // Edge at north
        #if !defined(SMAA_DISABLE_DIAG_DETECTION)
        // Diagonals have both north and west edges, so searching for them in
        // one of the boundaries is enough.
        weights.rg = SMAACalculateDiagWeights(edgesTex, areaTex, texcoord, e, subsampleIndices);

        // We give priority to diagonals, so if we find a diagonal we skip
        // horizontal/vertical processing.
        SMAA_BRANCH
        if (weights.r == -weights.g) { // weights.r + weights.g == 0.0
        #endif

        float2 d;

        // Find the distance to the left:
        float3 coords;
        coords.x = SMAASearchXLeft(edgesTex, searchTex, offset[0].xy, offset[2].x);
        coords.y = offset[1].y; // offset[1].y = texcoord.y - 0.25 * SMAA_RT_METRICS.y (@CROSSING_OFFSET)
        d.x = coords.x;

        // Now fetch the left crossing edges, two at a time using bilinear
        // filtering. Sampling at -0.25 (see @CROSSING_OFFSET) enables to
        // discern what value each edge has:
        float e1 = SMAASampleLevelZero(edgesTex, coords.xy).r;

        // Find the distance to the right:
        coords.z = SMAASearchXRight(edgesTex, searchTex, offset[0].zw, offset[2].y);
        d.y = coords.z;

        // We want the distances to be in pixel units:
        d = abs(round(mad(SMAA_RT_METRICS.zz, d, -pixcoord.xx)));

        // SMAAArea below needs a sqrt, as the areas texture is compressed
        // quadratically:
        float2 sqrt_d = sqrt(d);

        // Fetch the right crossing edges:
        float e2 = SMAASampleLevelZeroOffset(edgesTex, coords.zy, int2(1, 0)).r;

        // Get the actual area:
        weights.rg = SMAAArea(areaTex, sqrt_d, e1, e2, subsampleIndices.y);

        // Fix corners:
        coords.y = texcoord.y;
        SMAADetectHorizontalCornerPattern(edgesTex, weights.rg, coords.xyzy, d);

        #if !defined(SMAA_DISABLE_DIAG_DETECTION)
        } else
            e.r = 0.0; // Skip vertical processing.
        #endif
    }

    SMAA_BRANCH
    if (e.r > 0.0) { // Edge at west
        float2 d;

        // Find the distance to the top:
        float3 coords;
        coords.y = SMAASearchYUp(edgesTex, searchTex, offset[1].xy, offset[2].z);
        coords.x = offset[0].x; // offset[1].x = texcoord.x - 0.25 * SMAA_RT_METRICS.x;
        d.x = coords.y;

        // Fetch the top crossing edges:
        float e1 = SMAASampleLevelZero(edgesTex, coords.xy).g;

        // Find the distance to the bottom:
        coords.z = SMAASearchYDown(edgesTex, searchTex, offset[1].zw, offset[2].w);
        d.y = coords.z;

        // We want the distances to be in pixel units:
        d = abs(round(mad(SMAA_RT_METRICS.ww, d, -pixcoord.yy)));

        // SMAAArea below needs a sqrt, as the areas texture is compressed
        // quadratically:
        float2 sqrt_d = sqrt(d);

        // Fetch the bottom crossing edges:
        float e2 = SMAASampleLevelZeroOffset(edgesTex, coords.xz, int2(0, API_V_DIR(1))).g;

        // Get the area for this direction:
        weights.ba = SMAAArea(areaTex, sqrt_d, e1, e2, subsampleIndices.x);

        // Fix corners:
        coords.x = texcoord.x;
        SMAADetectVerticalCornerPattern(edgesTex, weights.ba, coords.xyxz, d);
    }

    return weights;
}

//-----------------------------------------------------------------------------
// Neighborhood Blending Pixel Shader (Third Pass)

float3 srgb_to_linear(float3 cs);
float3 linear_to_srgb(float3 cl);

float4 SMAANeighborhoodBlendingPS(float2 texcoord,
                                  float4 offset,
                                  Texture2D colorTex, SamplerState colorTexSampler,
                                  Texture2D blendTex, SamplerState blendTexSampler
                                  #if SMAA_REPROJECTION
                                  , Texture2D velocityTex, SamplerState velocityTexSampler
                                  #endif
                                  ) {
    // colorTexSampler/blendTexSampler are unused - see top-of-file comment.
    // Fetch the blending weights for current pixel - blendTex is SMAA's own
    // self-consistent internal buffer (written and read entirely within
    // SMAA's own passes), so texcoord/offset are used as-is here, unflipped.
    float4 a;
    a.x = SMAASample(blendTex, offset.xy).a; // Right
    a.y = SMAASample(blendTex, offset.zw).g; // Top
    a.wz = SMAASample(blendTex, texcoord).xz; // Bottom / Left

    // S24 (2026-08-19, live-test fix): colorTex here is the raw scene
    // buffer (src) - unlike blendTex above, this needs the same read-side
    // flip SMAAColorEdgeDetectionPS's matching fix documents (same GL-style
    // bottom-up storage, same fix pattern as glowcombineFXAAF.hlsl). Kept
    // as a separate coordinate rather than flipping texcoord itself, since
    // this function - unlike fxaaF.hlsl - samples two buffers with
    // DIFFERENT conventions in the same pass.
    float2 colorTexcoord = float2(texcoord.x, 1.0 - texcoord.y);

    // Is there any blending weight with a value greater than 0.0?
    SMAA_BRANCH
    if (dot(a, float4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
        float4 color = SMAASampleLevelZero(colorTex, colorTexcoord);
        color.rgb = srgb_to_linear(color.rgb);
        color.rgb = linear_to_srgb(color.rgb);
        return color;
    } else {
        bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)

        // Calculate the blending offsets:
        float4 blendingOffset = float4(0.0, API_V_DIR(a.y), 0.0, API_V_DIR(a.w));
        float2 blendingWeight = a.yw;
        SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0));
        SMAAMovc(bool2(h, h), blendingWeight, a.xz);
        blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

        // Calculate the texture coordinates (against colorTexcoord, not
        // texcoord - see comment above):
        float4 blendingCoord = mad(blendingOffset, float4(SMAA_RT_METRICS.xy, -SMAA_RT_METRICS.xy), colorTexcoord.xyxy);

        // We exploit bilinear filtering to mix current pixel with the chosen
        // neighbor:
        float4 color = SMAASampleLevelZero(colorTex, blendingCoord.xy);
        color.rgb = srgb_to_linear(color.rgb);
        color = blendingWeight.x * color;

        float4 color2 = SMAASampleLevelZero(colorTex, blendingCoord.zw);
        color2.rgb = srgb_to_linear(color2.rgb);
        color += blendingWeight.y * color2;

        color.rgb = linear_to_srgb(color.rgb);
        return color;
    }
}

#endif // SMAA_INCLUDE_PS
