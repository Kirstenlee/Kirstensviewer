/**
 * @file class1/deferred/cofF.hlsl
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

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (attached below, isDeferred=true
// on gDeferredCoFProgram) - moved this file's own texture to t7/s7.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

// depthMap/depthMapSampler are also declared by deferredUtil.hlsl - same
// real resource, guarded there - reuse it here (already matching names).
#ifndef LL_DEPTHMAP_DECLARED
#define LL_DEPTHMAP_DECLARED
Texture2D depthMap : register(t1);
SamplerState depthMapSampler : register(s1);
#endif

uniform float depth_cutoff;
uniform float norm_cutoff;
uniform float focal_distance;
uniform float blur_constant;
uniform float tan_pixel_angle;
uniform float magnification;
uniform float max_cof;

// inv_proj/screen_res are also declared by deferredUtil.hlsl, grouped
// together there under one guard. screen_res itself is never referenced in
// this file (confirmed via grep of both this file and the original GLSL),
// but must still be declared alongside inv_proj to match the exact same
// set deferredUtil.hlsl's guarded block declares - same reasoning as
// postDeferredF.hlsl's fix earlier this round.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

float calc_cof(float depth)
{
    float sc = (depth-focal_distance)/-depth*blur_constant;

    sc /= magnification;

    // tan_pixel_angle = pixel_length/-depth;
    float pixel_length =  tan_pixel_angle*-focal_distance;

    sc = sc/pixel_length;
    sc *= 1.414;

    return sc;
}

float4 main(PSInput IN) : SV_Target
{
    // S24 (2026-08-11, quick-win origin sweep): GL-vs-D3D11 texture-origin
    // flip - tc here is used only for the two direct Sample() calls below
    // (the depth read's NDC.xy is hardcoded to (0,0), not derived from tc),
    // so it's safe to flip once here rather than at each call site. Same
    // bug class as task #158/#185 (shadows/SSAO) - found via a proactive
    // sweep after those two fixes, never hit until DoF (task #140) is wired
    // in and exercised for real.
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);

    float z = depthMap.Sample(depthMapSampler, tc).r;
    // S24 (reversed-Z conversion, missed in the original r3704 sweep -
    // cofF.hlsl wasn't in that pass's file list): 1.0-z*2.0, was z*2.0-1.0 -
    // see deferredUtil.hlsl's linearDepth() comment for the full reasoning.
    // Left uncorrected, DoF's calc_cof() computed depth against an inverted
    // sense of near/far, throwing off the whole focal-plane/blur falloff.
    z = 1.0 - z*2.0;
    float4 ndc = float4(0.0, 0.0, z, 1.0);
    float4 p = mul(inv_proj, ndc);
    float depth = p.z/p.w;

    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc);

    float sc = calc_cof(depth);
    sc = min(sc, max_cof);
    sc = max(sc, -max_cof);

    float4 frag_color;
    frag_color.rgb = diff.rgb;
    frag_color.a = sc/max_cof*0.5+0.5;
    return frag_color;
}
