/**
 * @file class1/deferred/postDeferredNoDoFF.hlsl
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

// S24 (2026-08-17, task #139 live-test fix): this file previously
// implemented a 9-tap depth-weighted bilateral blur with a naive
// clamp(0,1) and no depth output - NOT what postDeferredNoDoFF.glsl (the
// real GLSL source this is supposed to be a port of) actually does. That
// drifted version was silently dormant since it was written (nothing ever
// bound+drew this shader in a live DX_RENDER frame until presentFinal()
// wired it in for real, dxpipeline.cpp) - first live test showed a hard
// red-orange tint over all real scene geometry (sky unaffected). Rewritten
// to faithfully match the GLSL: sample diffuse, optionally add a TINY
// dither (HAS_NOISE, +-0.003, not a spatial blur), clampHDRRange() (real
// HDR-range clamp, deferredUtil.hlsl - was wrongly hard-clamped to [0,1]
// before), and critically write depth out (gl_FragDepth in GLSL, SV_Depth
// here) so the swap chain's own depth buffer gets the scene's real depth -
// see DXRenderTarget.cpp's bindSwapChainBackBuffer() comment: later
// 3D-in-UI-space content (manipulator gizmos, etc.) depth-tests against
// this. Also added the GL/D3D11 texture-origin flip on vary_fragcoord -
// postDeferredF.hlsl (same vertex shader, postDeferredNoTCV.hlsl, same
// vary_fragcoord convention) already needed this exact fix (task #145
// sweep, 2026-08-11) and this file was never brought in line with it.

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (attached below, isDeferred=true
// on gDeferredPostNoDoFProgram/gDeferredPostNoDoFNoiseProgram) - moved this
// file's own diffuseRect to t7/s7.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

// depthMap/depthMapSampler are also declared by deferredUtil.hlsl - same
// real resource, guarded there - reuse it here (already matching names).
#ifndef LL_DEPTHMAP_DECLARED
#define LL_DEPTHMAP_DECLARED
Texture2D depthMap : register(t1);
SamplerState depthMapSampler : register(s1);
#endif

// inv_proj/screen_res are also declared by deferredUtil.hlsl, grouped
// together there under one guard. inv_proj itself is never referenced in
// this file, but must still be declared alongside screen_res to match the
// exact same set deferredUtil.hlsl's guarded block declares - same
// reasoning as this round's other fixes.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif

float3 clampHDRRange(float3 color);

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

//=================================
// borrowed noise from:
//  <https://www.shadertoy.com/view/4dS3Wd>
//  By Morgan McGuire @morgan3d, http://graphicscodex.com
//
float hash(float n) { return frac(sin(n) * 1e4); }
float hash(float2 p) { return frac(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }

float noise(float x) {
    float i = floor(x);
    float f = frac(x);
    float u = f * f * (3.0 - 2.0 * f);
    return lerp(hash(i), hash(i + 1.0), u);
}

float noise(float2 x) {
    float2 i = floor(x);
    float2 f = frac(x);

    // Four corners in 2D of a tile
    float a = hash(i);
    float b = hash(i + float2(1.0, 0.0));
    float c = hash(i + float2(0.0, 1.0));
    float d = hash(i + float2(1.0, 1.0));

    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

//=============================

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // S24 (2026-08-17): GL-vs-D3D11 texture-origin flip - see postDeferredF.hlsl's
    // identical fix (task #145 sweep, 2026-08-11). Both diffuseRect and
    // depthMap reads use tc below, matching that file's approach.
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);

    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc);

#ifdef HAS_NOISE
    float2 nc = tc * screen_res * 4.0;
    float3 seed = (diff.rgb + float3(1.0, 1.0, 1.0)) * float3(nc.xy, nc.x + nc.y);
    float3 nz = float3(noise(seed.rg), noise(seed.gb), noise(seed.rb));
    diff.rgb += nz * 0.003;
#endif

    diff.rgb = clampHDRRange(diff.rgb);
    OUT.color = diff;
    OUT.depth = depthMap.Sample(depthMapSampler, tc).r;
    return OUT;
}
