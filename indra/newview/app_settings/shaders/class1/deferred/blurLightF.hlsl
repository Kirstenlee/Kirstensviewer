/**
 * @file class1/deferred/blurLightF.hlsl
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

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (attached below) - moved this
// file's own texture to t7/s7, same free-slot convention established for
// the class2/3 light-shader family (pointLightF/spotLightF/etc.).
uniform Texture2D lightMap : register(t7);
uniform SamplerState lightMapSampler : register(s7);

uniform float dist_factor;
uniform float blur_size;
uniform float2 delta;

// screen_res is also declared (and used) by deferredUtil.hlsl, grouped
// there with inv_proj under one guard - this file never references
// inv_proj by name, but getPosition() (called below, implemented in
// deferredUtil.hlsl) uses it internally, so it's still genuinely needed
// by this shader instance. Must declare BOTH names here under the same
// guard, matching the paired set exactly - guarding only screen_res would
// let this block's #define silently skip deferredUtil.hlsl's later
// declaration of inv_proj too, leaving it completely undeclared (the
// exact self-inflicted split-guard mistake from earlier this session).
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float2 screen_res;
uniform float4x4 inv_proj;
#endif
uniform float3 kern[4];
uniform float kern_scale;

float4 getPosition(float2 pos_screen);
float4 getNorm(float2 pos_screen);

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord.xy;
    float4 norm = getNorm(tc);
    float3 pos = getPosition(tc).xyz;
    // S24 (2026-08-11, quick-win origin sweep): GL-vs-D3D11 texture-origin
    // flip - tc itself must stay unflipped (getNorm()/getPosition() above
    // already do their own internal flip and expect raw input, same
    // "shared coordinate used for two different purposes" pattern as
    // softenLightF.hlsl/aoUtil.hlsl elsewhere this session), so the flip is
    // inlined at each direct lightMap.Sample() call site only. Same bug
    // class as task #158/#185.
    float4 ccol = lightMap.Sample(lightMapSampler, float2(tc.x, 1.0 - tc.y)).rgba;

    float2 dlt = kern_scale * delta / (1.0 + norm.xy * norm.xy);
    dlt /= max(-pos.z * dist_factor, 1.0);

    float2 defined_weight = kern[0].xy;
    float4 col = float4(defined_weight.xyxx * ccol);

    float pointplanedist_tolerance_pow2 = pos.z * pos.z * 0.00005;

    tc *= screen_res;
    float tc_mod = 0.5 * (tc.x + tc.y);
    tc_mod -= floor(tc_mod);
    tc_mod *= 2.0;
    tc += ((tc_mod - 0.5) * kern[1].z * dlt * 0.5);

    float3 k[7];
    k[0] = kern[0];
    k[2] = kern[1];
    k[4] = kern[2];
    k[6] = kern[3];
    k[1] = (k[0] + k[2]) * 0.5f;
    k[3] = (k[2] + k[4]) * 0.5f;
    k[5] = (k[4] + k[6]) * 0.5f;

    for (int i = 1; i < 7; i++)
    {
        float2 samptc = tc + k[i].z * dlt * 2.0;
        samptc /= screen_res;
        float3 samppos = getPosition(samptc).xyz;
        float d = dot(norm.xyz, samppos.xyz - pos.xyz);
        if (d * d <= pointplanedist_tolerance_pow2)
        {
            col += lightMap.Sample(lightMapSampler, float2(samptc.x, 1.0 - samptc.y)) * k[i].xyxx;
            defined_weight += k[i].xy;
        }
    }

    // S24: renamed loop variable (was also "i") - HLSL's for-loop variable
    // scope leaks into the enclosing block (unlike C++'s own for-scope),
    // so reusing "i" here conflicted with the loop above (warning X3078).
    for (int j = 1; j < 7; j++)
    {
        float2 samptc = tc - k[j].z * dlt * 2.0;
        samptc /= screen_res;
        float3 samppos = getPosition(samptc).xyz;
        float d = dot(norm.xyz, samppos.xyz - pos.xyz);
        if (d * d <= pointplanedist_tolerance_pow2)
        {
            col += lightMap.Sample(lightMapSampler, float2(samptc.x, 1.0 - samptc.y)) * k[j].xyxx;
            defined_weight += k[j].xy;
        }
    }

    col /= float4(defined_weight.xyxx);
    return max(col, float4(0, 0, 0, 0));
}
