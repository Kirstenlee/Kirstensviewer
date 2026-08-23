/**
 * @file class1/deferred/CASF.hlsl
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

#ifndef A_CPU
#define A_GPU
#define A_HLSL
#define CAS_BETTER_DIAGONALS
#define CAS_SLOW

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);

uniform float2 out_screen_res;
uniform uint4 cas_param_0;
uniform uint4 cas_param_1;

float3 srgb_to_linear(float3 cs);
float3 linear_to_srgb(float3 cl);

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

// FidelityFX CAS - Contrast Adaptive Sharpening for HLSL
// Based on AMD FidelityFX CAS implementation
// See: https://github.com/GPUOpen-Effects/FidelityFX-CAS

// S24 (2026-08-19, task #231, task #227 audit finding): this was a from-
// scratch simplified box sharpen, not a port of AMD's real CasFilter()
// (CASF.glsl lines 2073-2202, the noScaling==true branch - the only branch
// this project's wrapper ever uses). Real bugs fixed: cas_param_0/1 were
// declared but never read at all (the user's sharpness setting had zero
// effect - GLSL's `peak` comes from `AF1_AU1(const1.x)`, a BIT-LEVEL
// reinterpret of cas_param_1.x as a float via CasSetup()'s packing, not a
// numeric cast - matched here with asfloat()); the min/max neighborhood
// was missing a real sample entirely (GLSL's diagonal a/c/g/i pass, added
// on top of the cross d/e/f/b/h pass under CAS_BETTER_DIAGONALS - this
// file's own #define at the top already enables that path, just never
// implemented it); the sqrt() amp-shaping step was missing; and the pass's
// output never went through linear_to_srgb() at all (added below, matching
// main()'s real tail: `diff.rgb = linear_to_srgb(diff.rgb);`).
//
// AMD's ffx_a.h fast-inverse/fast-sqrt bit-trick approximations
// (APrxLoRcpF1/APrxLoSqrtF1/APrxMedRcpF1, used because CAS_GO_SLOWER isn't
// defined upstream either) aren't ported here - real 1.0/x and sqrt() are
// used instead. This is the SAME configuration AMD's own CAS_GO_SLOWER
// path already uses (a real, supported, more-precise mode in the original
// library, not an invented shortcut) - correctness is unaffected, this
// pass just costs a little more GPU time than the bit-trick version would.
float4 main(PSInput IN) : SV_Target
{
    float2 pos = IN.vary_fragcoord;
    float2 rcpOut = 1.0 / out_screen_res;

    // 3x3 neighborhood, matching CASF.glsl's own a/b/c/d/e/f/g/h/i grid
    // layout exactly (top-left to bottom-right, row-major) - not reusing
    // the old code's different letter assignments, to avoid any risk of
    // silently transposing which sample is which.
    float3 a = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(-1, -1) * rcpOut, 0).rgb;
    float3 b = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2( 0, -1) * rcpOut, 0).rgb;
    float3 c = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2( 1, -1) * rcpOut, 0).rgb;
    float3 d = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(-1,  0) * rcpOut, 0).rgb;
    float3 e = diffuseRect.SampleLevel(diffuseRectSampler, pos, 0).rgb;
    float3 f = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2( 1,  0) * rcpOut, 0).rgb;
    float3 g = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(-1,  1) * rcpOut, 0).rgb;
    float3 h = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2( 0,  1) * rcpOut, 0).rgb;
    float3 i = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2( 1,  1) * rcpOut, 0).rgb;

    // Soft min/max over the cross (d,e,f,b,h), doubled via the diagonal
    // (a,c,g,i) pass - CAS_BETTER_DIAGONALS.
    float3 mn = min(min(d, e), f);
    mn = min(min(mn, b), h);
    float3 mn2 = min(min(mn, a), c);
    mn2 = min(min(mn2, g), i);
    mn = mn + mn2;

    float3 mx = max(max(d, e), f);
    mx = max(max(mx, b), h);
    float3 mx2 = max(max(mx, a), c);
    mx2 = max(max(mx2, g), i);
    mx = mx + mx2;

    float3 rcpM = 1.0 / mx;
    float3 amp = saturate(min(mn, 2.0 - mx) * rcpM);
    amp = sqrt(amp);

    float peak = asfloat(cas_param_1.x);
    float3 w = amp * peak;

    // CAS_SLOW (this file's own #define): per-channel weight/filter.
    float3 rcpWeight = 1.0 / (1.0 + 4.0 * w);
    float3 color = saturate((b * w + d * w + f * w + h * w + e) * rcpWeight);

    color = linear_to_srgb(color);

    float alpha = diffuseRect.SampleLevel(diffuseRectSampler, pos, 0).a;
    return float4(color, alpha);
}
#endif
