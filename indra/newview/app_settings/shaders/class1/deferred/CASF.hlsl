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
    float2 vary_fragcoord : TEXCOORD0;
};

// FidelityFX CAS - Contrast Adaptive Sharpening for HLSL
// Based on AMD FidelityFX CAS implementation
// See: https://github.com/GPUOpen-Effects/FidelityFX-CAS

float4 main(PSInput IN) : SV_Target
{
    // CAS provides contrast adaptive sharpening
    // Using the implementation from the FidelityFX CAS SDK
    // The GLSL version defines A_GPU and A_GLSL - we define A_GPU and A_HLSL instead

    float2 pos = IN.vary_fragcoord;

    // Load 4 pixels for sharpening
    float2 rcpOut = 1.0 / out_screen_res;

    // Fetch 3x3 neighborhood for CAS
    float3 b = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(-1, -1) * rcpOut, 0).rgb;
    float3 d = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(0, -1) * rcpOut, 0).rgb;
    float3 e = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(1, -1) * rcpOut, 0).rgb;
    float3 f = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(-1, 0) * rcpOut, 0).rgb;
    float3 g = diffuseRect.SampleLevel(diffuseRectSampler, pos, 0).rgb;
    float3 h = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(1, 0) * rcpOut, 0).rgb;
    float3 i = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(-1, 1) * rcpOut, 0).rgb;
    float3 j = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(0, 1) * rcpOut, 0).rgb;
    float3 k = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(1, 1) * rcpOut, 0).rgb;

    // CAS sharpen
    float3 mn = min(min(min(d, e), min(f, h)), min(min(g, b), min(i, k)));
    float3 mx = max(max(max(d, e), max(f, h)), max(max(g, b), max(i, k)));

    float3 rcpMx = float3(1.0, 1.0, 1.0) / mx;
    float3 amp = saturate(min(mn, 2.0 - mx) * rcpMx);
    float3 peak = -float3(1.0 / (4.0 * 6.0 - 1.0), 1.0 / (4.0 * 6.0 - 1.0), 1.0 / (4.0 * 6.0 - 1.0));

    float3 w = amp * peak;

    float3 rcpWeight = 1.0 / (w.x + w.y + w.z + 1.0);
    float3 color = (w.x * (b + e + h + k) + w.y * (f + h + d + i) + w.z * (d + f + i + k) + g) * rcpWeight;

    return float4(color, 1.0);
}
#endif
