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

// S24: per-pixel multi-probe blend, ported from class3/deferred/reflectionProbeF.glsl -
// nearest-probe bucket search (refBucket, depth-indexed), box/sphere influence-volume
// intersection, distance+angular attenuation weighting, automatic-vs-manual probe
// blending, neighbor-probe blending. SSR (doProbeSample()/sampleReflectionProbesLegacy()'s
// #ifdef SSR blocks, tapScreenSpaceReflection() body in screenSpaceReflUtil.hlsl) included.
//
// Still NOT ported (unrelated features):
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
// S24: register(b1), not b0 - b0 is always the auto-generated $Globals cbuffer for this
// shader's top-level "uniform" declarations (LLRender's hardcoded
// VSSetConstantBuffers(0,...)/PSSetConstantBuffers(0,...), llrender.cpp). This cbuffer's
// field order/types must byte-match llreflectionmapmanager.h's ReflectionProbeData struct
// exactly - float4x4/float4/int4 pack the same as std140 mat4/vec4/ivec4.

#define FLT_MAX 3.402823466e+38
#define MAX_REFMAP_COUNT 256

TextureCube environmentMap : register(t9);
SamplerState environmentMapSampler : register(s9);

// S24: t16/t17 SRV slots (ps_5_0 SRV slots go up to t127, so plenty of room). Both textures
// reuse environmentMapSampler (s9) rather than declaring their own s16/s17 - unlike SRV
// slots, D3D11 pixel-shader SAMPLER slots are hard-capped at 16 (s0-s15), so s16/s17 don't
// exist. Safe to share since all three are cubemap-shaped, bound CLAMP+TRILINEAR
// (DXSampler::getOrCreate(2,2)) - same fix as pbrterrainF.hlsl's identical bug class.
TextureCubeArray reflectionProbes : register(t16);
TextureCubeArray irradianceProbes : register(t17);

// S24: tapScreenSpaceReflection()'s body lives in screenSpaceReflUtil.hlsl (always
// co-attached whenever SSR is defined). sceneMap reuses environmentMapSampler (s9) rather
// than a new sampler slot - same D3D11 s0-s15 cap reasoning as reflectionProbes/
// irradianceProbes above; t19 is the next free SRV slot after t16/t17.
#ifdef SSR
float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness);
Texture2D sceneMap : register(t19);

// S24: tunable, not a hardcoded `glossiness >= 0.9` literal - pbralphaF.hlsl's
// `perceptualRoughness = max(orm.g * roughnessFactor, 0.3)` floor caps glossiness at 0.7,
// so a fixed 0.9 gate made SSR unreachable for that material at any setting.
// screenSpaceReflUtil.hlsl's own vignette (`clamp(glossiness*3-1.7,0,1)`) already fades
// SSR out below ~0.567.
uniform float ssrGlossThreshold;
#endif

// S24: also declared by softenLightF.hlsl (co-attached for the deferred lighting combine)
// - guarded since this file is also attached standalone to forward/alpha shaders
// (pbralphaF.hlsl, alphaF.hlsl, materialF.hlsl) that never include softenLightF.hlsl.
#ifndef LL_CUBE_SNAPSHOT_DECLARED
#define LL_CUBE_SNAPSHOT_DECLARED
uniform int cube_snapshot;
#endif

uniform float reflection_probe_ambiance;
uniform float max_probe_lod;
// S24: classic_mode also declared (guarded, same macro name) by deferredUtil.hlsl, always
// co-attached with this file.
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
uniform float probe_equalize;           // Blend toward this probe's own fully-averaged top mip (0.0-1.0, default 0.0) - see tapRefMap()
uniform float probe_opacity;            // Dedicated visibility blend: 1.0=fully visible, 0.0=fully transparent (default 1.0)

// S24: must byte-match llreflectionmapmanager.h's ReflectionProbeData struct exactly -
// same field order and types (float4x4~LLMatrix4, float4~LLVector4, int4~GLint[4]).
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

        // S24: dw ramps 0->1 across the outer ~17% of the box's own volume (d>=0.83), so
        // manual box probes decisively override the automatic/void fallback near their own
        // walls/corners. Must stay proportional to box-local `d` (already normalized [0,1])
        // - do NOT scale by an absolute real-world radius like sphereWeight() does for
        // spheres (`w` there is unbounded inverse distance, a different unit entirely);
        // mixing an absolute term into this normalized value causes a hard-edged seam.
        // A residual per-face content mismatch near corners may be a separate cubemap-
        // capture issue (see radianceGenF.hlsl's face-ID diagnostic), not this weight.
        dw = saturate(d * 6.0);
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

    // S24: no post-env_mat sign correction here, deliberately - a known front/back
    // reflection reversal exists, but every x/y/z negation combination tried either fixed
    // it while breaking east/west, or vice versa. env_mat's own construction and the
    // per-face capture tables (DXCubeMapFaces::sUpVecs) are independently verified correct;
    // do not add a sign flip here without new evidence pinpointing which face/axis is
    // actually wrong (suspect: captured cubemap content or array-index/mip selection, not
    // this function's direction math).

    // S24: lod must be clamped before SampleLevel()'s EXPLICIT LOD param - unlike Sample()'s
    // automatic LOD, SampleLevel() does not clamp an out-of-range level to the texture's
    // real mip count, so an unclamped bias reads past max_probe_lod and comes back black.
    float adjusted_lod = clamp(lod + probe_blur_lod_bias, 0.0, max_probe_lod);
    float4 ret = reflectionProbes.SampleLevel(environmentMapSampler, float4(v.xyz, (float)refIndex[i].x), adjusted_lod) * refParams[i].y;

    // S24: probe_equalize blends toward a second sample at this same probe's own top mip
    // (max_probe_lod) - the fully GGX-convolved, whole-hemisphere average (see
    // radianceGenF.hlsl's prefilterEnvMap()) - to even out per-face brightness/color using
    // this probe's own real averaged tone, unlike probe_saturation/probe_contrast which
    // pull toward an arbitrary neutral. Default 0.0 preserves full detail/hue.
    if (probe_equalize > 0.0)
    {
        float4 equalized = reflectionProbes.SampleLevel(environmentMapSampler, float4(v.xyz, (float)refIndex[i].x), max_probe_lod) * refParams[i].y;
        ret = lerp(ret, equalized, saturate(probe_equalize));
    }

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

    // S24: no post-env_mat sign correction here either - see tapRefMap()'s matching comment.
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

    // S24: probe_opacity is a plain visibility blend (0.0=this probe contributes nothing,
    // 1.0=no change) - distinct from probe_intensity above, which can boost past 1.0.
    result *= saturate(probe_opacity);

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

// S24: this is the original GLSL's own "#else" (HERO_PROBES not defined) fallback stub,
// kept unconditional since this file never declares HERO_PROBES itself.
//
#if defined(HERO_PROBES)

// S24: heroProbes reuses environmentMapSampler (s9), same convention as every other
// cubemap-shaped texture in this file; register t20 is the next free slot after t16-t19.
//
// S24: named `heroClipPlane`, NOT `clipPlane` (GLSL's name) - collides with Water Shader's
// own unrelated `clipPlane` uniform (water-plane clipping) when both are concatenated,
// causing an X3003 redefinition. LLHeroProbeManager::mCurrentClipPlane is tracked in C++
// but not currently uploaded to any shader; until wired up this falls back to clipDist=0
// (handled safely below, just without real edge falloff).
uniform float4 heroClipPlane;
TextureCubeArray heroProbes : register(t20);

void tapHeroProbe(inout float3 glossenv, float3 pos, float3 norm, float glossiness)
{
    // S24: perf - the lerp below always collapses to w=0 (no-op) for glossiness<=0.75 (only a
    // quarter of the mips are generated for hero probes, so anything below that threshold gets
    // zero weight regardless of clipDist/box/sphere result) - skip the box/sphere intersect and
    // cubemap sample entirely rather than computing and discarding them every pixel.
    if (glossiness <= 0.75)
    {
        return;
    }

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

    // S24: cube_snapshot != 1 avoids recursively sampling the screen during a reflection-
    // probe capture itself.
#ifdef SSR
    if (cube_snapshot != 1 && glossiness >= ssrGlossThreshold)
    {
        // S24: dim the cube sample before the lerp below rather than zeroing it - on a real
        // SSR hit (ssr.a -> 1) the lerp result is still ~100% ssr.rgb regardless of this
        // factor, but on a miss (ssr.a == 0) it fills in with a dim cube value instead of
        // black (e.g. water's sky reflection, a near-guaranteed miss at grazing angles).
        static const float kSSRCubeFillWeight = 0.25;
        glossenv *= kSSRCubeFillWeight;

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
    // S24: probes_enabled==0 fallback mirrors sampleReflectionProbesLegacy()'s own fallback
    // and doProbeSample()'s SSR-tap/hero-probe structure (transparent=false, matching
    // water's own call to it further below) - keep both branches structurally equivalent.
    if (probes_enabled == 0)
    {
        float3 refnormpersp = reflect(pos.xyz, norm.xyz);

        ambenv = amblit_linear;
        if (classic_mode == 0)
            ambenv = sampleProbeAmbient(pos, norm, amblit_linear);

        float3 env_vec = mul(env_mat, normalize(refnormpersp));
        // S24: glossenv is always linear-space, so this sample must go through
        // srgb_to_linear() here - unlike legacyenv (sampleReflectionProbesLegacy()'s
        // fallback below), which applyLegacyEnv() expects raw/un-converted. Both patterns
        // exist side by side in the GL reference; don't copy one convention to the other.
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
    // S24: debug volume visualization (sphereIntersectDebug/boxIntersectDebug/
    // debugTapRefMap) not ported - decorative RENDER_DEBUG_REFLECTION_PROBES overlay only.
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
        // S24: probes_enabled==0 samples environmentMap (t9) directly instead of the probe
        // array, which is uncaptured/stale in that state - environmentMap is still bound
        // every frame by the legacy branch in LLPipeline::bindDeferredShader().
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

    // S24: no glossiness threshold here, unlike doProbeSample()'s SSR block above - the
    // legacy/bump-shiny path taps SSR for any glossiness, matching the GL reference.
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
        // S24: GLSL also blends SSR into legacyenv here, not just glossenv - legacyenv
        // feeds applyLegacyEnv() (legacy/bump-shiny materials).
        legacyenv = lerp(legacyenv, ssr.rgb, ssr.a);
    }
#endif

    tapHeroProbe(glossenv, pos, norm, glossiness);
    // S24: perf - hardcoding glossiness=1.0 here defeated tapHeroProbe()'s own <=0.75 early-out,
    // paying a full box/sphere-intersect + cubemap sample every pixel even though legacyenv is
    // only ever consumed downstream when envIntensity>0 (applyLegacyEnv() callers all gate on it).
    // Match that same gate here instead of always tapping.
    if (envIntensity > 0.0)
    {
        tapHeroProbe(legacyenv, pos, norm, 1.0);
    }

    // S24: legacyenv feeds applyLegacyEnv() directly (its lerp does not clamp the blend
    // target) - built from the same probe/SSR/hero-probe samples as glossenv just below,
    // but was missing the equivalent clamp. fullbrightShinyF.hlsl's applyLegacyEnv() call
    // (old texture + Fullbright + any legacy Shininess) has no direct-specular term to
    // dilute this against - it's the color, full stop - so an unclamped HDR sample there
    // (SSR/reflection probe catching the sun, an overexposed sky patch) blows straight to
    // white. Matches user-observed repro: old texture + Fullbright + Shiny = white face.
    glossenv = clamp(glossenv, float3(0, 0, 0), float3(10, 10, 10));
    legacyenv = clamp(legacyenv, float3(0, 0, 0), float3(10, 10, 10));
}

void applyGlossEnv(inout float3 color, float3 glossenv, float4 spec, float3 pos, float3 norm)
{
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
    // S24: must match GLSL exactly - real Fresnel term (grazing angles reflect more,
    // head-on less), *0.5 blend, no srgb round-trip.
    float3 reflected_color = legacyenv;
    float3 lookAt = normalize(pos);
    float fresnel = 1.0 + dot(lookAt, norm.xyz);
    fresnel *= fresnel;
    fresnel = min(fresnel + envIntensity, 1.0);
    reflected_color *= (envIntensity * fresnel);
    color = lerp(color.rgb, reflected_color * 0.5, envIntensity);
}
