/**
 * @file class1/deferred/dofCombineF.hlsl
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
// on gDeferredDoFCombineProgram) - moved this file's own textures to t7/t8
// (lightMap here is a different resource from deferredUtil.hlsl's depthMap,
// just accidentally sharing t1 - a register move, not a guard).
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);
Texture2D lightMap : register(t8);
SamplerState lightMapSampler : register(s8);

// inv_proj/screen_res are also declared by deferredUtil.hlsl, grouped
// together there under one guard. inv_proj itself is never referenced in
// this file, but must still be declared alongside screen_res to match the
// exact same set deferredUtil.hlsl's guarded block declares - same
// reasoning as postDeferredF.hlsl/cofF.hlsl's fixes this round.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif

uniform float max_cof;
uniform float res_scale;
uniform float dof_width;
uniform float dof_height;

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

float4 dofSample(Texture2D tex, SamplerState texSampler, float2 tc)
{
    tc.x = min(tc.x, dof_width);
    tc.y = min(tc.y, dof_height);

    // S24 (2026-08-11, quick-win origin sweep): GL-vs-D3D11 texture-origin
    // flip - applied after the dof_width/dof_height edge clamp above (those
    // bounds are unrelated to Y-origin convention), immediately before the
    // actual .Sample() call. Same bug class as task #158/#185. Callers pass
    // their raw/unflipped coordinate in, matching the established
    // "flip only at the .Sample() call site" pattern.
    tc.y = 1.0 - tc.y;

    return tex.Sample(texSampler, tc);
}

float4 main(PSInput IN) : SV_Target
{
    float4 dof = dofSample(diffuseRect, diffuseRectSampler, IN.vary_fragcoord.xy*res_scale);

    // S24 (2026-08-11, quick-win origin sweep): same flip as dofSample()
    // above, inlined at each direct lightMap.Sample() call site (this file
    // has no position-reconstruction use of vary_fragcoord to preserve).
    float4 diff = lightMap.Sample(lightMapSampler, float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y));

    float a = min(abs(diff.a*2.0-1.0) * max_cof*res_scale*res_scale, 1.0);

    if (a > 0.25 && a < 0.75)
    { //help out the transition a bit
        float sc = a/res_scale;

        float2 flipped_fragcoord = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);
        float4 col;
        col = lightMap.Sample(lightMapSampler, flipped_fragcoord+float2(sc,-sc)/screen_res);
        col += lightMap.Sample(lightMapSampler, flipped_fragcoord+float2(-sc,-sc)/screen_res);
        col += lightMap.Sample(lightMapSampler, flipped_fragcoord+float2(sc,sc)/screen_res);
        col += lightMap.Sample(lightMapSampler, flipped_fragcoord+float2(-sc,sc)/screen_res);

        diff = lerp(diff, col*0.25, a);
    }

    return lerp(diff, dof, a);
}
