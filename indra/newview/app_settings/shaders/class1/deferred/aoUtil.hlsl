/**
 * @file class1/deferred/aoUtil.hlsl
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

// noiseMap is a genuinely different resource from deferredUtil.hlsl's
// normalMap (SSAO rotation-noise texture vs. G-buffer normal channel) -
// they only collided by accident of both independently landing on t0/s0.
// Moved to t4/s4, clear of deferredUtil.hlsl (t0-t3) and shadowUtil.hlsl
// (t10-t15, attached alongside this whenever sunLightF.hlsl also has
// HAS_SUN_SHADOW) - "Deferred Sun Shader" doesn't attach gbufferUtil.hlsl/
// reflectionProbeF.hlsl, so t4-t9 is otherwise free here.
uniform Texture2D noiseMap : register(t4);
uniform SamplerState noiseSampler : register(s4);

// depthMap/depthMapSampler are also declared by deferredUtil.hlsl - same
// real resource (the original GLSL redundantly declares an identical
// "uniform sampler2D depthMap" too), guarded there - reuse it here. Renamed
// this file's own sampler from depthSampler to depthMapSampler to match
// deferredUtil.hlsl's naming exactly (a guard only merges identical
// declarations - it can't reconcile two different names for the same
// resource, so getDepthAo()'s body below is updated to match).
#ifndef LL_DEPTHMAP_DECLARED
#define LL_DEPTHMAP_DECLARED
uniform Texture2D depthMap : register(t1);
uniform SamplerState depthMapSampler : register(s1);
#endif

uniform float ssao_radius;
uniform float ssao_max_radius;
uniform float ssao_factor;
uniform float ssao_factor_inv;

// inv_proj/screen_res are also declared by deferredUtil.hlsl, grouped
// together there under one guard - reuse it here.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif

float2 getScreenCoordinateAo(float2 screenpos)
{
    float2 sc = screenpos.xy * 2.0;
    return sc - float2(1.0, 1.0);
}

float getDepthAo(float2 pos_screen)
{
    // S24 (2026-08-10, task #158/#184 follow-up): same GL-vs-D3D11 texture-
    // origin flip already applied everywhere else this session that reads
    // a G-buffer/depth/lightmap render target with a screen-space UV
    // (getGBuffer()/getDepth() in gbufferUtil.hlsl/deferredUtil.hlsl,
    // softenLightF.hlsl's lightMap sample) - this file was never updated
    // with it. pos_screen itself stays unflipped (getScreenCoordinateAo()
    // below uses it for NDC/position reconstruction, which must stay in
    // the camera's own convention, same "two different uses of the same
    // screen coordinate" split already established for vary_fragcoord
    // elsewhere) - the flip is inlined at this .Sample() call site only,
    // not carried by the parameter.
    float depth = depthMap.Sample(depthMapSampler, float2(pos_screen.x, 1.0 - pos_screen.y)).r;
    return depth;
}

float4 getPositionAo(float2 pos_screen)
{
    float depth = getDepthAo(pos_screen);
    float2 sc = getScreenCoordinateAo(pos_screen);
    float4 ndc = float4(sc.x, sc.y, 2.0 * depth - 1.0, 1.0);
    float4 pos = mul(inv_proj, ndc);
    pos /= pos.w;
    pos.w = 1.0;
    return pos;
}

float2 getKern(int i)
{
    float2 kern[8];
    // exponentially (^2) distant occlusion samples spread around origin
    kern[0] = float2(-1.0, 0.0) * 0.125 * 0.125;
    kern[1] = float2(1.0, 0.0) * 0.250 * 0.250;
    kern[2] = float2(0.0, 1.0) * 0.375 * 0.375;
    kern[3] = float2(0.0, -1.0) * 0.500 * 0.500;
    kern[4] = float2(0.7071, 0.7071) * 0.625 * 0.625;
    kern[5] = float2(-0.7071, -0.7071) * 0.750 * 0.750;
    kern[6] = float2(-0.7071, 0.7071) * 0.875 * 0.875;
    kern[7] = float2(0.7071, -0.7071) * 1.000 * 1.000;

    return kern[i] / screen_res;
}

// Calculate decreases in ambient lighting when crowded out (SSAO)
float calcAmbientOcclusion(float4 pos, float3 norm, float2 pos_screen)
{
    float ret = 1.0;
    float3 pos_world = pos.xyz;

    // S24 (2026-08-19, task #229, task #227 audit finding): GLSL early-outs
    // here for distant pixels (SSAO has negligible effect beyond ~64m) -
    // this HLSL copy was missing it entirely and always ran the full
    // 8-sample kernel regardless of distance, producing a real (potentially
    // non-1.0) occlusion value for far geometry that GLSL always keeps at
    // exactly 1.0 (no occlusion).
    if (-pos_world.z > 64.0)
    {
        return 1.0;
    }

    // S24 (2026-08-10, task #158/#184 follow-up): same texture-origin flip
    // as getDepthAo() above - noiseMap is a tiled rotation-noise texture
    // sampled with a screen-space UV, same class of read as depth/G-buffer
    // samples elsewhere. Confirmed root cause of a real, reported "upside
    // down pixel dither" artifact tied specifically to SSAO (disappears
    // when SSAO is disabled) - the unflipped tiled noise pattern read
    // mismatched screen rows, producing a visibly wrong, static-looking
    // dither instead of the intended per-pixel kernel rotation.
    float2 noise_reflect = noiseMap.Sample(noiseSampler, float2(pos_screen.x, 1.0 - pos_screen.y) * (screen_res / 128)).xy;

    float angle_hidden = 0.0;
    float points = 0;

    float scale = min(ssao_radius / -pos_world.z, ssao_max_radius);

    // it was found that keeping # of samples a constant was the fastest, probably due to compiler optimizations (unrolling?)
    for (int i = 0; i < 8; i++)
    {
        float2 samppos_screen = pos_screen + scale * reflect(getKern(i), noise_reflect);
        float3 samppos_world = getPositionAo(samppos_screen).xyz;

        float3 diff = pos_world - samppos_world;
        float dist2 = dot(diff, diff);

        // assume each sample corresponds to an occluding sphere with constant radius, constant x-sectional area
        // --> solid angle shrinking by the square of distance
        //radius is somewhat arbitrary, can approx with just some constant k * 1 / dist^2
        //(k should vary inversely with # of samples, but this is taken care of later)

        float funky_val = (dot((samppos_world - 0.05 * norm - pos_world), norm) > 0.0) ? 1.0 : 0.0;
        angle_hidden = angle_hidden + funky_val * min(1.0 / dist2, ssao_factor_inv);

        // 'blocked' samples (significantly closer to camera relative to pos_world) are "no data", not "no occlusion"
        float diffz_val = (diff.z > -1.0) ? 1.0 : 0.0;
        points = points + diffz_val;
    }

    angle_hidden = min(ssao_factor * angle_hidden / points, 1.0);

    float points_val = (points > 0.0) ? 1.0 : 0.0;
    ret = (1.0 - (points_val * angle_hidden));

    ret = max(ret, 0.0);
    return min(ret, 1.0);
}
