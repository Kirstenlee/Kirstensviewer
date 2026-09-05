/**
 * @file class3/deferred/reflectionProbeF.hlsl
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

// S24 (2026-08-09, task #147b): real per-pixel multi-probe blend - a close
// port of class3/deferred/reflectionProbeF.glsl (961 lines), replacing
// task #147 v1's simplified single-slice sample. History: this file was an
// unreachable placeholder until 2026-08-05's emergency stabilization (a
// minimal single-legacy-cubemap fallback, to get PSInput redefinition
// crashes fixed), then task #147 v1 (2026-08-09) added a real but
// simplified TextureCubeArray sample (array slice 0 only, no per-pixel
// probe selection). This is the real thing: nearest-probe bucket search
// (refBucket, depth-indexed), box/sphere influence-volume intersection,
// distance+angular attenuation weighting, automatic-vs-manual probe
// blending, neighbor-probe blending.
//
// S24 (2026-08-11, task #156): SSR restored - see doProbeSample()/
// sampleReflectionProbesLegacy()'s own #ifdef SSR blocks below, and
// screenSpaceReflUtil.hlsl for tapScreenSpaceReflection()'s real body
// (was a debug stub). Was deliberately deferred at the time the comment
// below was written ("its own body of work") - this is that follow-up.
//
// Still NOT ported (out of scope for #156 too, unrelated features):
//   - Hero probes (#if defined(HERO_PROBES) - mirrors) - tapHeroProbe() is
//     kept as the original's own unconditional empty-body fallback (matches
//     the GLSL's own "#else" branch when HERO_PROBES isn't defined), so the
//     call sites in doProbeSample()/sampleReflectionProbesLegacy() don't
//     need touching if hero probes are added later - the cbuffer's own
//     heroBox/heroSphere/heroShape/heroMipCount/heroProbeCount fields ARE
//     still declared (layout must match llreflectionmapmanager.h's
//     ReflectionProbeData struct exactly - this is a direct byte-level
//     constant-buffer upload, not name-based reflection), just never read.
//   - Debug volume visualization (sphereIntersectDebug/boxIntersectDebug/
//     debugTapRefMap/the real sampleReflectionProbesDebug body) - kept as
//     the existing "show nothing" stub, a decorative debug-overlay feature,
//     not the actual reflection math.
//
// Real D3D11 constant buffer backing this (llreflectionmapmanager.cpp's
// updateUniforms()/setUniforms(), DXBuffer-based) - register(b1), since
// register(b0) is always the auto-generated $Globals cbuffer for whatever
// top-level "uniform" declarations this file/its host shader has (see
// LLRender's hardcoded VSSetConstantBuffers(0,...)/PSSetConstantBuffers(0,...),
// llrender.cpp). This cbuffer's field order/types MUST match
// llreflectionmapmanager.h's ReflectionProbeData struct exactly, byte for
// byte - float4x4/float4/int4 pack identically to std140 mat4/vec4/ivec4
// (both default to the same "4-byte-tight, 16-byte-aligned" rules for
// vector-shaped array elements), confirmed by direct reasoning about both
// APIs' packing rules, not assumed.

#define FLT_MAX 3.402823466e+38
#define MAX_REFMAP_COUNT 256

TextureCube environmentMap : register(t9);
SamplerState environmentMapSampler : register(s9);

// S24 (2026-08-09): t16/t17 chosen after a full-tree register audit (see
// task #147 v1's own comment, still accurate) - t16 confirmed free for
// every shader that attaches this file, t17 is the next slot and equally
// unused project-wide (grep-confirmed). ps_5_0 SRV slots go up to t127, so
// t16/t17 are fine.
//
// CORRECTION (fixed a real X4509 compile failure): an earlier version of
// this comment argued each texture needs its own sampler at the matching
// register (s16/s17), reasoning from LLTexUnit::bind(LLCubeMapArray*)'s
// PSSetSamplers(mIndex,...) pattern. That's wrong - unlike SRV slots,
// D3D11 pixel-shader SAMPLER slots are hard-capped at 16 (s0-s15), so
// s16/s17 don't exist and failed to compile. This is the exact same
// bug class already hit and fixed once in pbrterrainF.hlsl (task #114,
// see that file's own comment) - the fix there and here is the same:
// reuse an existing, already-validly-bound sampler with compatible
// filter settings rather than inventing a new out-of-range slot. Both
// reflectionProbes and irradianceProbes sample via environmentMapSampler
// (s9) - safe because all three are cubemap-shaped textures bound
// CLAMP+TRILINEAR (DXSampler::getOrCreate(2,2)), so the sampler object
// itself doesn't need to be texture-specific. (LLTexUnit::bind(LLCubeMapArray*)'s
// own `if (mIndex < 16)` guard on its PSSetSamplers call already
// anticipated this cap - it just wasn't propagated back to this
// declaration when these two textures were added for #147b.)
TextureCubeArray reflectionProbes : register(t16);
TextureCubeArray irradianceProbes : register(t17);

// S24 (2026-08-11, task #156): SSR restored - see this file's own earlier
// "Deliberately NOT ported... SSR/hero probe is its own body of work"
// header comment, and the session plan (task #156 section) for the full
// investigation. tapScreenSpaceReflection()'s real body now lives in
// screenSpaceReflUtil.hlsl (always co-attached whenever SSR is defined,
// same "SSR" permutation gDeferredSoftenProgram/every PBR-lit shader
// already receives via llviewershadermgr.cpp's shared attribs map).
// sceneMap reuses environmentMapSampler (s9) rather than declaring a new
// sampler - same "D3D11 samplers cap at s15, reuse an already-bound
// compatible one" reasoning as reflectionProbes/irradianceProbes just
// above (t19 chosen as the next free SRV slot after t16/t17, same "grep-
// confirmed free project-wide" discipline).
#ifdef SSR
float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness);
Texture2D sceneMap : register(t19);

// S24 (2026-09-03, task #266/#271): was a bare `glossiness >= 0.9` literal at
// both call sites below. That's structurally unreachable for pbralphaF.hlsl
// (the PBR alpha-blend material class - confirmed via live RenderDoc shader-
// debug this session to be the exact shader behind the box-probe floor this
// whole investigation started from): its own `perceptualRoughness = max(orm.g
// * roughnessFactor, 0.3)` floor caps glossiness at 0.7, so SSR could never
// fire for that material at ANY setting under the old hardcoded gate. Made
// tunable instead of just lowering the literal, since screenSpaceReflUtil.hlsl's
// own internal vignette term (`clamp(glossiness*3-1.7,0,1)`) already fades SSR
// out below ~0.567 - the right value depends on live testing, not a guess.
uniform float ssrGlossThreshold;
#endif

// S24 (2026-08-11, task #156): also declared (unguarded) by softenLightF.hlsl,
// which is always co-attached whenever this file is used for the deferred
// lighting combine - guarded here (and there) with the same include-guard
// pattern already established throughout this codebase for dual-declared
// uniforms, since this file is ALSO attached to forward/alpha shaders
// (pbralphaF.hlsl, alphaF.hlsl, materialF.hlsl) that never include
// softenLightF.hlsl at all.
#ifndef LL_CUBE_SNAPSHOT_DECLARED
#define LL_CUBE_SNAPSHOT_DECLARED
uniform int cube_snapshot;
#endif

uniform float reflection_probe_ambiance;
uniform float max_probe_lod;
// S24 (2026-08-09): classic_mode is also declared (guarded) by deferredUtil.hlsl,
// which is always co-attached with this file (isDeferred||hasReflectionProbes
// vs. hasReflectionProbes - a strict subset). Must use the exact same guard
// macro name so whichever file concatenates first wins - see that file's own
// comment on this same uniform.
#ifndef LL_CLASSIC_MODE_DECLARED
#define LL_CLASSIC_MODE_DECLARED
uniform int classic_mode;
#endif
uniform int probes_enabled; // S24: Toggle to disable probe reflections entirely

// S24: Runtime reflection probe tweaking uniforms
uniform float probe_intensity;          // Overall reflection strength (0.0-2.0, default 1.0)
uniform float probe_saturation;         // Color saturation (0.0-2.0, default 1.0)
uniform float probe_contrast;           // Contrast adjustment (0.0-2.0, default 1.0)
uniform float probe_blur_lod_bias;      // Blur via LOD bias (-3.0 to 3.0, default 0.0)
uniform float probe_ambient_multiplier; // Ambient contribution (0.0-2.0, default 1.0)

// S24 (2026-08-09, task #147b): must byte-match llreflectionmapmanager.h's
// ReflectionProbeData struct exactly - same field order, same types
// (float4x4~LLMatrix4, float4~LLVector4, int4~GLint[4]/GLint[256][4] which
// are themselves already ivec4-shaped in the C++ struct - see that
// header's own "should always match reflectionProbeF.glsl" comment).
cbuffer ReflectionProbes : register(b1)
{
    // for box probes, matrix that transforms from camera space to a [-1, 1] cube representing the bounding box of
    // the box probe
    float4x4 refBox[MAX_REFMAP_COUNT];

    float4x4 heroBox;

    // for sphere probes, origin (xyz) and radius (w) of refmaps in clip space
    float4 refSphere[MAX_REFMAP_COUNT];

    // extra parameters
    //  x - irradiance scale
    //  y - radiance scale
    //  z - fade in
    //  w - znear
    float4 refParams[MAX_REFMAP_COUNT];

    float4 heroSphere;

    // indices used by probe:
    //  [i].x - cubemap array index for this probe
    //  [i].y - index into refNeighbor for probes that intersect this probe
    //  [i].z - number of probes that intersect this probe, or -1 for no neighbors
    //  [i].w - priority (probe type stored in sign bit - positive for spheres, negative for boxes)
    int4 refIndex[MAX_REFMAP_COUNT];

    // list of neighbor indices
    int4 refNeighbor[1024];

    // lookup table for which index to start with for the given Z depth
    int4 refBucket[256];

    // number of active refmaps
    int refmapCount;

    int heroShape;
    int heroMipCount;
    int heroProbeCount;
};

// Inputs
#ifndef LL_ENV_MAT_DECLARED
#define LL_ENV_MAT_DECLARED
uniform float3x3 env_mat;
#endif

float3 srgb_to_linear(float3 c);
float3 linear_to_srgb(float3 c);

// list of probeIndexes shader will actually use after "preProbeSample" is called
// (stores refIndex/refSphere indices, NOT reflectionProbes layer)
static int probeIndex[REF_SAMPLE_COUNT];

// number of probes stored in probeIndex
static int probeInfluences = 0;

// S24: HLSL globals default to implicitly-const unless marked static -
// see waterF.hlsl's own comment on this same GLSL-vs-HLSL difference.
static bool sample_automatic = true;

// return true if probe at index i influences position pos
bool shouldSampleProbe(int i, float3 pos)
{
    if (refIndex[i].w < 0)
    {
        float4 v = mul(refBox[i], float4(pos, 1.0));
        if (abs(v.x) > 1 ||
            abs(v.y) > 1 ||
            abs(v.z) > 1)
        {
            return false;
        }

        // never allow automatic probes to encroach on box probes
        sample_automatic = false;
    }
    else
    {
        if (refIndex[i].w == 0 && !sample_automatic)
        {
            return false;
        }

        float3 delta = pos.xyz - refSphere[i].xyz;
        float d = dot(delta, delta);
        float r2 = refSphere[i].w;
        r2 *= r2;

        if (d > r2)
        { // outside bounding sphere
            return false;
        }
    }

    return true;
}

int getStartIndex(float3 pos)
{
    int idx = clamp((int)floor(-pos.z), 0, 255);
    return clamp(refBucket[idx].x, 1, refmapCount + 1);
}

// call before sampleProbes/sampleProbeAmbient
// populate "probeIndex" with N probe indices that influence pos where N is REF_SAMPLE_COUNT
void preProbeSample(float3 pos)
{
#if REFMAP_LEVEL > 0

    int start = getStartIndex(pos);

    // TODO: make some sort of structure that reduces the number of distance checks
    for (int i = start; i < refmapCount; ++i)
    {
        // found an influencing probe
        if (shouldSampleProbe(i, pos))
        {
            probeIndex[probeInfluences] = i;
            ++probeInfluences;

            int neighborIdx = refIndex[i].y;
            if (neighborIdx != -1)
            {
                int neighborCount = refIndex[i].z;

                int count = 0;
                while (count < neighborCount)
                {
                    // check up to REF_SAMPLE_COUNT-1 neighbors (neighborIdx is int4 index)

                    // sample refNeighbor[neighborIdx].x
                    int idx = refNeighbor[neighborIdx].x;
                    if (shouldSampleProbe(idx, pos))
                    {
                        probeIndex[probeInfluences++] = idx;
                        if (probeInfluences == REF_SAMPLE_COUNT)
                        {
                            break;
                        }
                    }
                    count++;
                    if (count == neighborCount)
                    {
                        break;
                    }

                    // sample refNeighbor[neighborIdx].y
                    idx = refNeighbor[neighborIdx].y;
                    if (shouldSampleProbe(idx, pos))
                    {
                        probeIndex[probeInfluences++] = idx;
                        if (probeInfluences == REF_SAMPLE_COUNT)
                        {
                            break;
                        }
                    }
                    count++;
                    if (count == neighborCount)
                    {
                        break;
                    }

                    // sample refNeighbor[neighborIdx].z
                    idx = refNeighbor[neighborIdx].z;
                    if (shouldSampleProbe(idx, pos))
                    {
                        probeIndex[probeInfluences++] = idx;
                        if (probeInfluences == REF_SAMPLE_COUNT)
                        {
                            break;
                        }
                    }
                    count++;
                    if (count == neighborCount)
                    {
                        break;
                    }

                    // sample refNeighbor[neighborIdx].w
                    idx = refNeighbor[neighborIdx].w;
                    if (shouldSampleProbe(idx, pos))
                    {
                        probeIndex[probeInfluences++] = idx;
                        if (probeInfluences == REF_SAMPLE_COUNT)
                        {
                            break;
                        }
                    }
                    count++;

                    ++neighborIdx;
                }

                break;
            }
        }
    }

    if (sample_automatic)
    { // probe at index 0 is a special probe for smoothing out automatic probes
        probeIndex[probeInfluences++] = 0;
    }
#else
    probeIndex[probeInfluences++] = 0;
#endif
}

// adapted -- assume that origin is inside sphere, return intersection of ray with edge of sphere
float3 sphereIntersect(float3 origin, float3 dir, float3 center, float radius2)
{
    float t0, t1; // solutions for t if the ray intersects

    float3 L = center - origin;
    float tca = dot(L, dir);

    float d2 = dot(L, L) - tca * tca;

    float thc = sqrt(radius2 - d2);
    t0 = tca - thc;
    t1 = tca + thc;

    float3 v = origin + dir * t1;
    return v;
}

// get point of intersection with given probe's box influence volume
// origin - ray origin in clip space
// dir - ray direction in clip space
// i - probe's refBox matrix
// d - distance to nearest wall in clip space
// scale - scale of box, default 1.0
float3 boxIntersect(float3 origin, float3 dir, float4x4 i, out float d, float scale)
{
    // Intersection with OBB convert to unit box space
    // Transform in local unit parallax cube space (scaled and rotated)
    float4x4 clipToLocal = i;

    float3 RayLS = mul((float3x3)clipToLocal, dir);
    float3 PositionLS = mul(clipToLocal, float4(origin, 1.0)).xyz;

    d = 1.0 - max(max(abs(PositionLS.x), abs(PositionLS.y)), abs(PositionLS.z));

    float3 Unitary = float3(scale, scale, scale);
    float3 FirstPlaneIntersect = (Unitary - PositionLS) / RayLS;
    float3 SecondPlaneIntersect = (-Unitary - PositionLS) / RayLS;
    float3 FurthestPlane = max(FirstPlaneIntersect, SecondPlaneIntersect);
    float Distance = min(FurthestPlane.x, min(FurthestPlane.y, FurthestPlane.z));

    // Use Distance in CS directly to recover intersection
    float3 IntersectPositionCS = origin + dir * Distance;

    return IntersectPositionCS;
}

float3 boxIntersect(float3 origin, float3 dir, float4x4 i, out float d)
{
    return boxIntersect(origin, dir, i, d, 1.0);
}

// get the weight of a sphere probe
//  pos - position to be weighted
//  dir - normal to be weighted
//  origin - center of sphere probe
//  r - radius of probe influence volume
//  i - refParams entry for this probe
//  dw - distance weight
float sphereWeight(float3 pos, float3 dir, float3 origin, float r, float4 i, out float dw)
{
    float r1 = r * 0.5; // 50% of radius (outer sphere to start interpolating down)
    float3 delta = pos.xyz - origin;
    float d2 = max(length(delta), 0.001);

    float atten = 1.0 - max(d2 - r1, 0.0) / max((r - r1), 0.001);
    float w = 1.0 / d2;

    w *= i.z;

    dw = w * atten * max(r, 1.0) * 4;

    w *= atten;

    return w;
}

// Tap a reflection probe
// pos - position of pixel
// dir - pixel normal
//  w - weight of sample (distance and angular attenuation)
//  dw - weight of sample (distance only)
// lod - which mip to sample (lower is higher res, sharper reflections)
// c - center of probe
// i - index of probe
float3 tapRefMap(float3 pos, float3 dir, out float w, out float dw, float lod, float3 c, int i)
{
    // parallax adjustment
    float3 v;

    if (refIndex[i].w < 0)
    {  // box probe
        float d = 0;
        v = boxIntersect(pos, dir, refBox[i], d);

        w = max(d, 0.001);

        // S24 (2026-08-15, task #194): `dw` (distance-only weight, used by
        // sampleProbes()'s automatic-vs-manual blend as
        // lerp(col[0], col[1], min(dwsum[1], 1.0))) was never written on
        // this branch - real, confirmed gap (present in reflectionProbeF.glsl
        // too, not a DX_RENDER-only porting bug), leaving it genuinely
        // uninitialized per HLSL/GLSL's own semantics for an `out` param no
        // caller ever wrote. Fixed to mirror the sphere branch's own
        // convention (below) so manual probes decisively override automatic
        // ones within their volume, matching shouldSampleProbe()'s already-
        // stated intent ("never allow automatic probes to encroach on box
        // probes").
        //
        // S24 (2026-09-03, task #266/#271): a follow-up fix here (`dw = w *
        // max(r, 1.0) * 4.0`, mirroring sphereWeight()'s own `* max(r,1.0)`
        // term) turned out to be a units mismatch, not a valid mirror -
        // REVERTED 2026-09-04 after a live regression report (a hard-edged
        // "square" seam visible in water reflections around large
        // structures, screenshots "water gloss.png"/"water gloss2.png").
        // sphereWeight()'s `w` is 1/d2 - INVERSE REAL-WORLD DISTANCE from the
        // probe center, unbounded, so multiplying by `r` there rescales it
        // back toward a sane magnitude. Box's `w` (== `d` here) is already a
        // normalized [0,1] box-local fraction (1 = center, 0 = at the wall)
        // - multiplying THAT by an absolute real-world radius `r` (tens of
        // meters for a building-sized probe) makes dw reach 1.0 (fully
        // manual, zero blend with the void/automatic fallback) within a
        // sliver `d >= 1/(4r)` of the box surface - for a large probe that's
        // an almost-instant snap, not a fade, which is exactly what a hard
        // visible edge looks like. The ORIGINAL bug this was chasing (task
        // #266/#271: old flat `w*4.0` made every box's automatic-fallback
        // fade a fixed, size-independent 25% shell, washing out large rooms'
        // floors) is still real and still needs solving, but scaling by `r`
        // isn't the fix - proportional-only (no absolute-size term at all)
        // is: ramp dw from 0 to 1 across the outer 50% of `d` (mirrors
        // sphereWeight()'s OWN "r1 = r*0.5" convention conceptually, i.e.
        // fade starts at the 50%-of-the-way-to-center mark - just expressed
        // in box's already-normalized units instead of sphere's real-meter
        // ones), which is both proportional for any probe size AND actually
        // wider than the old buggy 25% shell (matches manual reflections
        // more decisively near floors/walls than before, the original goal).
        dw = saturate(d * 2.0);
    }
    else
    { // sphere probe
        float r = refSphere[i].w;

        float rr = r * r;

        v = sphereIntersect(pos, dir, c,
        refIndex[i].w < 1 ? 4096.0 * 4096.0 : // <== effectively disable parallax correction for automatically placed probes to keep from bombing the world with obvious spheres
                rr);

        w = sphereWeight(pos, dir, refSphere[i].xyz, r, refParams[i], dw);
    }

    v -= c;
    float3 d3 = normalize(v);

    v = mul(env_mat, v);

    // S24 (task #194, 2026-08-14): a direct x/z (and later x/y/z) negation
    // was tried here across several rounds to fix a confirmed front/back
    // reflection reversal (controlled two-wall test: reflections showed
    // what's beyond the object instead of behind the viewer). Every
    // combination tried (x/z only, x/y/z, with and without the
    // llviewerwindow.cpp/llreflectionmapmanager.cpp viewport Y-flip
    // restored) either left it upside down or broke east/west again -
    // never both correct at once. Reverted to no post-env_mat correction
    // (the original, pre-2026-08-14 behavior: front/back reversed, but
    // top/bottom and the rest of the pipeline in a known-clean state) while
    // a face-ID color diagnostic is added to actually trace which physical
    // cube face lands where, instead of guessing more sign combinations.
    // env_mat's own construction and the per-face capture tables
    // (DXCubeMapFaces::sUpVecs) both independently verified correct - do
    // not touch either without new evidence.

    // S24 (2026-08-15, task #194 offshoot): a 3-round visual diagnostic
    // sequence lived here (PositionLS validity, `v` magnitude, `v`
    // direction-as-color) - all three came back clean/coherent for box
    // probes, ruling out a degenerate value or gross sign/axis error
    // anywhere in this function's own math. Removed per diagnostic-
    // lifecycle convention; see [[project_dxrender_open_issues]]/
    // memorygraph for the full round-by-round result if this needs
    // revisiting. Next suspect: captured cubemap content or array-index/
    // mip selection, not this function's direction math.

    // S24: Apply LOD bias for blur/sharpness control
    float adjusted_lod = lod + probe_blur_lod_bias;
    float4 ret = reflectionProbes.SampleLevel(environmentMapSampler, float4(v.xyz, (float)refIndex[i].x), adjusted_lod) * refParams[i].y;

    return ret.rgb;
}

// Tap an irradiance map
// pos - position of pixel
// dir - pixel normal
// w - weight of sample (distance and angular attenuation)
// dw - weight of sample (distance only)
// c - center of probe
// i - index of probe
// amblit - fallback ambient if this probe doesn't fully replace it
float3 tapIrradianceMap(float3 pos, float3 dir, out float w, out float dw, float3 c, int i, float3 amblit)
{
    // parallax adjustment
    float3 v;
    if (refIndex[i].w < 0)
    {
        float d = 0.0;
        v = boxIntersect(pos, dir, refBox[i], d, 3.0);
        w = max(d, 0.001);

        // S24 (2026-08-15, task #194): see tapRefMap()'s identical fix -
        // `dw` was never written on this branch either, same real
        // uninitialized-out-param gap (present in reflectionProbeF.glsl
        // too), same fix.
        dw = w * 4.0;
    }
    else
    {
        float r = refSphere[i].w; // radius of sphere volume

        // pad sphere for manual probe extending into automatic probe space
        float rr = r * r;

        v = sphereIntersect(pos, dir, c,
        refIndex[i].w < 1 ? 4096.0 * 4096.0 : // <== effectively disable parallax correction for automatically placed probes to keep from bombing the world with obvious spheres
                rr);

        w = sphereWeight(pos, dir, refSphere[i].xyz, r, refParams[i], dw);
    }

    v -= c;
    v = mul(env_mat, v);

    // S24 (task #194, 2026-08-14): sign-flip experiments reverted, see
    // tapRefMap()'s matching comment - a face-ID color diagnostic is being
    // added instead of guessing more combinations.

    float3 col = irradianceProbes.SampleLevel(environmentMapSampler, float4(v.xyz, (float)refIndex[i].x), 0).rgb * refParams[i].x;

    col = lerp(amblit, col, min(refParams[i].x, 1.0));

    return col;
}

float3 sampleProbes(float3 pos, float3 dir, float lod)
{
    // S24: Early out if probes disabled
    if (probes_enabled == 0)
    {
        return float3(0, 0, 0);
    }

    float wsum[2];
    wsum[0] = 0;
    wsum[1] = 0;

    float dwsum[2];
    dwsum[0] = 0;
    dwsum[1] = 0;

    float3 col[2];
    col[0] = float3(0, 0, 0);
    col[1] = float3(0, 0, 0);

    for (int idx = 0; idx < probeInfluences; ++idx)
    {
        int i = probeIndex[idx];
        int p = clamp(abs(refIndex[i].w), 0, 1);

        if (p == 0 && !sample_automatic)
        {
            continue;
        }

        float w = 0;
        float dw = 0;
        float3 refcol;

        {
            refcol = tapRefMap(pos, dir, w, dw, lod, refSphere[i].xyz, i);

            col[p] += refcol.rgb * w;
            wsum[p] += w;
            dwsum[p] += dw;
        }
    }

    // mix automatic and manual probes
    if (sample_automatic && wsum[0] > 0.0)
    { // some automatic probes were sampled
        col[0] *= 1.0 / wsum[0];
        if (wsum[1] > 0.0)
        { //some manual probes were sampled, mix between the two
            col[1] *= 1.0 / wsum[1];
            col[1] = lerp(col[0], col[1], min(dwsum[1], 1.0));
            col[0] = float3(0, 0, 0);
        }
    }
    else if (wsum[1] > 0.0)
    {
        // manual probes were sampled but no automatic probes were
        col[1] *= 1.0 / wsum[1];
        col[0] = float3(0, 0, 0);
    }

    float3 result = col[1] + col[0];

    // S24: Apply reflection probe tweaks
    // Intensity
    result *= probe_intensity;

    // Saturation
    if (probe_saturation != 1.0)
    {
        float luma = dot(result, float3(0.299, 0.587, 0.114));
        result = lerp(float3(luma, luma, luma), result, probe_saturation);
    }

    // Contrast
    if (probe_contrast != 1.0)
    {
        result = (result - 0.5) * probe_contrast + 0.5;
        result = max(result, float3(0.0, 0.0, 0.0)); // Clamp to prevent negative values
    }

    return result;
}

float3 sampleProbeAmbient(float3 pos, float3 dir, float3 amblit)
{
    // S24: Early out if probes disabled
    if (probes_enabled == 0)
    {
        return amblit;
    }

    // modified copy/paste of sampleProbes follows, will likely diverge from sampleProbes further
    // as irradiance map mixing is tuned independently of radiance map mixing
    float wsum[2];
    wsum[0] = 0;
    wsum[1] = 0;

    float dwsum[2];
    dwsum[0] = 0;
    dwsum[1] = 0;

    float3 col[2];
    col[0] = float3(0, 0, 0);
    col[1] = float3(0, 0, 0);

    for (int idx = 0; idx < probeInfluences; ++idx)
    {
        int i = probeIndex[idx];
        int p = clamp(abs(refIndex[i].w), 0, 1);

        if (p == 0 && !sample_automatic)
        {
            continue;
        }

        {
            float w = 0;
            float dw = 0;

            float3 refcol = tapIrradianceMap(pos, dir, w, dw, refSphere[i].xyz, i, amblit);

            col[p] += refcol * w;
            wsum[p] += w;
            dwsum[p] += dw;
        }
    }

    // mix automatic and manual probes
    if (sample_automatic && wsum[0] > 0.0)
    { // some automatic probes were sampled
        col[0] *= 1.0 / wsum[0];
        if (wsum[1] > 0.0)
        { //some manual probes were sampled, mix between the two
            col[1] *= 1.0 / wsum[1];
            col[1] = lerp(col[0], col[1], min(dwsum[1], 1.0));
            col[0] = float3(0, 0, 0);
        }
    }
    else if (wsum[1] > 0.0)
    {
        // manual probes were sampled but no automatic probes were
        col[1] *= 1.0 / wsum[1];
        col[0] = float3(0, 0, 0);
    }

    float3 result = col[1] + col[0];

    // S24: Apply ambient multiplier to probe ambient contribution
    result *= probe_ambient_multiplier;

    return result;
}

// S24 (2026-08-09, task #147b): hero probes (mirrors) deliberately not
// ported - see this file's header comment. This is the original GLSL's
// own "#else" (HERO_PROBES not defined) fallback, kept unconditional here
// since this file never declares HERO_PROBES.
//
#if defined(HERO_PROBES)

// S24 (2026-08-22): real port of reflectionProbeF.glsl's HERO_PROBES branch
// (mirrors) - this function was an unconditional empty stub regardless of
// whether HERO_PROBES was defined for the compiled permutation, even though
// llviewershadermgr.cpp sets attribs["HERO_PROBES"]="1" whenever RenderMirrors
// is on (shared, backend-agnostic - matches GLSL exactly). LLHeroProbeManager
// is a real, fully-working DX-native capture pipeline (6-face capture,
// gaussian blur, mip chain, radiance-gen convolution, all confirmed via
// CopySubresourceRegion translations already in place) that ran every frame
// and was correctly bound to this shader's HERO_PROBE texture channel
// (LLPipeline::bindReflectionProbes()) - but nothing ever sampled it, so
// mirrors fell back 100% to the generic low-res (128px) reflection-probe
// cubemap. heroProbes register is the next free slot after this file's
// existing t16-t19 (reflectionProbes/irradianceProbes/sceneDepth/sceneMap);
// reuses environmentMapSampler (s9), same convention as every other
// cubemap-shaped texture in this file.
//
// S24 (2026-08-22, CTD fix): named `heroClipPlane`, NOT `clipPlane` - GLSL
// uses the generic name, but that collides with Water Shader's own
// unrelated `clipPlane` uniform (water-plane clipping, a different concept)
// once this file is attached to a shader that also pulls in that
// declaration. Confirmed via a real D3DCompile failure (X3003 redefinition
// of 'clipPlane', "Water Shader") that left mDXVertexShader null and
// crashed on the next LLHLSLShader::bind() assert. LLHeroProbeManager::
// mCurrentClipPlane is tracked in C++ but not currently uploaded to any
// shader (a separate, likely pre-existing gap on both backends, not unique
// to this port); until that's wired up this falls back to a clipDist of 0,
// which the formula below still handles safely (just without real edge
// falloff).
uniform float4 heroClipPlane;
TextureCubeArray heroProbes : register(t20);

void tapHeroProbe(inout float3 glossenv, float3 pos, float3 norm, float glossiness)
{
    float clipDist = dot(pos.xyz, heroClipPlane.xyz) + heroClipPlane.w;
    float w = 0;
    float dw = 0;
    float falloffMult = 10;
    float3 refnormpersp = reflect(pos.xyz, norm.xyz);
    if (heroShape < 1)
    {
        float d = 0;
        boxIntersect(pos, norm, heroBox, d, 1.0);

        w = max(d, 0);
    }
    else
    {
        float r = heroSphere.w;

        w = sphereWeight(pos, refnormpersp, heroSphere.xyz, r, float4(1, 1, 1, 1), dw);
    }

    clipDist = clipDist * 0.95 + 0.05;
    clipDist = clamp(clipDist * falloffMult, 0, 1);
    w = clamp(w * falloffMult * clipDist, 0, 1);
    w = lerp(0, w, clamp(glossiness - 0.75, 0, 1) * 4); // We only generate a quarter of the mips for the hero probes.  Linearly interpolate between normal probes and hero probes based upon glossiness.
    float3 heroSample = heroProbes.SampleLevel(environmentMapSampler, float4(mul(env_mat, refnormpersp), 0), (1.0 - glossiness) * heroMipCount).xyz;
    glossenv = lerp(glossenv, heroSample, w);
}

#else

// CORRECTION (fixed a real, persistent X3508 compile failure): two earlier
// attempts kept `glossenv` as an `inout` output parameter and tried to
// satisfy FXC's completeness check with a no-op body (first a literal
// `glossenv = glossenv;`, then a read-into-temp/write-back-from-temp
// version) - both still errored at the exact same location. Root cause:
// this function is a genuine, provable no-op, so FXC's dead-code
// elimination strips any such assignment back out before the
// completeness check runs, leaving nothing for the checker to see no
// matter how the no-op is phrased. Since the stub never actually needs
// to write anything (hero probes aren't implemented FOR THIS permutation -
// see the #if defined(HERO_PROBES) branch above for the real one), the real
// fix is to stop declaring `glossenv` as an output at all - pass it by
// value instead of `inout`. No output parameter, no completeness check, and
// call sites are unaffected since this function never modified their
// value anyway.
void tapHeroProbe(float3 glossenv, float3 pos, float3 norm, float glossiness)
{
}

#endif

void doProbeSample(inout float3 ambenv, inout float3 glossenv,
        float2 tc, float3 pos, float3 norm, float glossiness, bool transparent, float3 amblit)
{
    // TODO - don't hard code lods
    float reflection_lods = max_probe_lod;

    float3 refnormpersp = reflect(pos.xyz, norm.xyz);

    ambenv = amblit;

    if (classic_mode == 0)
        ambenv = sampleProbeAmbient(pos, norm, amblit);

    float lod = (1.0 - glossiness) * reflection_lods;
    glossenv = sampleProbes(pos, normalize(refnormpersp), lod);

    // S24 (2026-08-11, task #156): restored - matches reflectionProbeF.glsl's
    // #if defined(SSR) block exactly (only for near-mirror surfaces,
    // glossiness >= 0.9, and never during a reflection-probe capture itself
    // - cube_snapshot != 1 - which would otherwise recursively sample the
    // screen it's currently rendering into).
#ifdef SSR
    if (cube_snapshot != 1 && glossiness >= ssrGlossThreshold)
    {
        float4 ssr = float4(0, 0, 0, 0);
        if (transparent)
        {
            tapScreenSpaceReflection(1, tc, pos, norm, ssr, sceneMap, environmentMapSampler, 1);
            ssr.a *= glossiness;
        }
        else
        {
            tapScreenSpaceReflection(1, tc, pos, norm, ssr, sceneMap, environmentMapSampler, glossiness);
        }

        glossenv = lerp(glossenv, ssr.rgb, ssr.a);
    }
#endif

    tapHeroProbe(glossenv, pos, norm, glossiness);
}

void sampleReflectionProbes(inout float3 ambenv, inout float3 glossenv,
        float2 tc, float3 pos, float3 norm, float glossiness, bool transparent, float3 amblit_linear)
{
    preProbeSample(pos);
    doProbeSample(ambenv, glossenv, tc, pos, norm, glossiness, transparent, amblit_linear);
}

void sampleReflectionProbesWater(inout float3 ambenv, inout float3 glossenv,
        float2 tc, float3 pos, float3 norm, float glossiness, float3 amblit_linear)
{
    // S24 (2026-08-31, task #197): was unconditional - this always sampled
    // the probe array via doProbeSample() below regardless of probes_enabled,
    // giving water dark/flat reflections whenever the array wasn't actually
    // being captured. Mirrors sampleReflectionProbesLegacy()'s existing
    // probes_enabled==0 fallback (task #194) - environmentMap/env_mat are
    // now guaranteed live and bound whenever this branch runs, see
    // LLPipeline::shouldUseLegacyEnvMap()'s comment (pipeline.h/.cpp) for
    // why that wasn't previously true for every case this condition covers.
    // Mirrors doProbeSample()'s own SSR-tap/hero-probe structure below too
    // (transparent=false, same as water's own call to it further down) so
    // both branches produce an equivalent final result, not just an
    // equivalent base sample.
    if (probes_enabled == 0)
    {
        float3 refnormpersp = reflect(pos.xyz, norm.xyz);

        ambenv = amblit_linear;
        if (classic_mode == 0)
            ambenv = sampleProbeAmbient(pos, norm, amblit_linear);

        float3 env_vec = mul(env_mat, normalize(refnormpersp));
        // S24 (2026-09-04, task #200 investigation): this raw legacy-cubemap
        // sample was going straight into glossenv unconverted - every other
        // glossenv producer in this shader family (class2's real GL-reference
        // sampleReflectionProbes(), doProbeSample()'s sampleProbes() calls)
        // treats glossenv as linear-space data, converting via
        // srgb_to_linear() right at the sample point. This branch was modeled
        // on sampleReflectionProbesLegacy()'s own probes_enabled==0 fallback
        // (see that function's comment) - correct for THAT function, where
        // the raw sample feeds legacyenv/applyLegacyEnv()'s special
        // mix-then-single-convert roundtrip (confirmed against the pristine
        // GL reference, S:\Dev\XREF Other Source\S24 Backout - class2's
        // reflectionProbeF.glsl has both patterns side by side: converting
        // for glossenv, raw for legacyenv). Copied the sampling call but not
        // the color-space handling that made it correct there - glossenv
        // here was silently too bright/wrong-contrast whenever this fallback
        // engaged (RenderReflectionProbesEnabled off, water reflections).
        glossenv = srgb_to_linear(environmentMap.Sample(environmentMapSampler, env_vec).rgb);

#ifdef SSR
        if (cube_snapshot != 1 && glossiness >= ssrGlossThreshold)
        {
            float4 ssr = float4(0, 0, 0, 0);
            tapScreenSpaceReflection(1, tc, pos, norm, ssr, sceneMap, environmentMapSampler, glossiness);
            glossenv = lerp(glossenv, ssr.rgb, ssr.a);
        }
#endif
        tapHeroProbe(glossenv, pos, norm, glossiness);
        return;
    }

    // don't sample automatic probes for water
    sample_automatic = false;
    preProbeSample(pos);
    sample_automatic = true;
    // always include void probe on water
    probeIndex[probeInfluences++] = 0;

    doProbeSample(ambenv, glossenv, tc, pos, norm, glossiness, false, amblit_linear);
}

float4 sampleReflectionProbesDebug(float3 pos)
{
    // S24 (2026-08-09, task #147b): debug volume visualization
    // (sphereIntersectDebug/boxIntersectDebug/debugTapRefMap) deliberately
    // not ported - show nothing, same as task #147 v1's stub. This is a
    // decorative debug-overlay feature (RENDER_DEBUG_REFLECTION_PROBES),
    // not the actual reflection math.
    return float4(0, 0, 0, 0);
}

void sampleReflectionProbesLegacy(inout float3 ambenv, inout float3 glossenv, inout float3 legacyenv,
        float2 tc, float3 pos, float3 norm, float glossiness, float envIntensity, bool transparent, float3 amblit_linear)
{
    float reflection_lods = max_probe_lod;
    preProbeSample(pos);

    float3 refnormpersp = reflect(pos.xyz, norm.xyz);

    ambenv = amblit_linear;

    if (classic_mode == 0)
        ambenv = sampleProbeAmbient(pos, norm, amblit_linear);

    if (glossiness > 0.0)
    {
        float lod = (1.0 - glossiness) * reflection_lods;
        glossenv = sampleProbes(pos, normalize(refnormpersp), lod);
    }

    if (envIntensity > 0.0)
    {
        // S24 (2026-08-15, task #194 offshoot): this always sampled the
        // probe array (sampleProbes()), even when probes are disabled -
        // confirmed via full-file read that this file's own environmentMap
        // (t9, declared above) was never sampled anywhere, despite being
        // bound every frame by the legacy branch in
        // LLPipeline::bindDeferredShader() (pipeline.cpp). class2's copy
        // of this same function already samples environmentMap correctly
        // (env_vec = mul(env_mat, normalize(reflect(pos,norm))); .Sample()) -
        // ported that path here, gated on the same probes_enabled uniform
        // setUniforms() now correctly writes to 0 when
        // sReflectionProbesEnabled is off, instead of leaving the probe
        // array (uncaptured/stale in that state) as the only source.
        if (probes_enabled == 0)
        {
            float3 env_vec = mul(env_mat, normalize(refnormpersp));
            legacyenv = environmentMap.Sample(environmentMapSampler, env_vec).rgb;
        }
        else
        {
            legacyenv = sampleProbes(pos, normalize(refnormpersp), 0.0);
        }
    }

    // S24 (2026-08-11, task #156): restored - matches reflectionProbeF.glsl's
    // #if defined(SSR) block in sampleReflectionProbesLegacy() exactly (no
    // glossiness threshold here, unlike doProbeSample()'s block above - the
    // legacy/bump-shiny path taps SSR for any glossiness, matching the
    // original). This is the block task #163 (shiny reflections not
    // appearing on the legacy/bump path) is most likely to interact with.
#ifdef SSR
    if (cube_snapshot != 1)
    {
        float4 ssr = float4(0, 0, 0, 0);

        if (transparent)
        {
            tapScreenSpaceReflection(1, tc, pos, norm, ssr, sceneMap, environmentMapSampler, 1);
            ssr.a *= glossiness;
        }
        else
        {
            tapScreenSpaceReflection(1, tc, pos, norm, ssr, sceneMap, environmentMapSampler, glossiness);
        }

        glossenv = lerp(glossenv, ssr.rgb, ssr.a);
        // S24 (2026-08-19, task #237, task #227 audit finding): GLSL also
        // blends SSR into legacyenv here (reflectionProbeF.glsl:930), right
        // after the glossenv line above - this file only had the glossenv
        // half. legacyenv feeds applyLegacyEnv() (legacy/bump-shiny
        // materials), so SSR contributions never reached that path at all
        // under DX_RENDER - this comment block above already names task
        // #163 as the most likely thing this interacts with; probable real
        // root cause.
        legacyenv = lerp(legacyenv, ssr.rgb, ssr.a);
    }
#endif

    tapHeroProbe(glossenv, pos, norm, glossiness);
    tapHeroProbe(legacyenv, pos, norm, 1.0);

    glossenv = clamp(glossenv, float3(0, 0, 0), float3(10, 10, 10));
}

void applyGlossEnv(inout float3 color, float3 glossenv, float4 spec, float3 pos, float3 norm)
{
    // S24 (2026-08-09, task #147b): real logic - task #147 v1 (and the
    // emergency-stabilization stub before it) left this completely empty
    // (a no-op), so glossenv never actually contributed to bump/shiny
    // materials' final color even once real probe data existed. Ported
    // directly from reflectionProbeF.glsl - unrelated to the cube-array
    // work itself, just a genuinely dead function found along the way.
    glossenv *= 0.5; // fudge darker
    float fresnel = clamp(1.0 + dot(normalize(pos.xyz), norm.xyz), 0.3, 1.0);
    fresnel *= fresnel;
    fresnel *= spec.a;
    glossenv *= spec.rgb * fresnel;
    glossenv *= float3(1.0, 1.0, 1.0) - color; // fake energy conservation
    color.rgb += glossenv * 0.5;
}

void applyLegacyEnv(inout float3 color, float3 legacyenv, float4 spec, float3 pos, float3 norm, float envIntensity)
{
    // S24 (2026-08-15, task #194 offshoot): this had diverged from
    // reflectionProbeF.glsl's real formula - was a flat
    // lerp(color, legacyenv*2.0, envIntensity) with no Fresnel term and
    // 2.0x instead of GLSL's 0.5x (a real 4x brightness difference on top
    // of the missing view-angle falloff). Restored to match GLSL exactly:
    // real Fresnel (grazing angles reflect more, head-on less), *0.5
    // blend, no srgb round-trip (GLSL's mix() doesn't do one either).
    float3 reflected_color = legacyenv;
    float3 lookAt = normalize(pos);
    float fresnel = 1.0 + dot(lookAt, norm.xyz);
    fresnel *= fresnel;
    fresnel = min(fresnel + envIntensity, 1.0);
    reflected_color *= (envIntensity * fresnel);
    color = lerp(color.rgb, reflected_color * 0.5, envIntensity);
}
