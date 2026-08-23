/**
 * @file class1/interface/twotexturecompareF.hlsl
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

Texture2D tex0 : register(t0);
SamplerState tex0Sampler : register(s0);
Texture2D tex1 : register(t1);
SamplerState tex1Sampler : register(s1);
Texture2D dither_tex : register(t2);
SamplerState dither_texSampler : register(s2);
uniform float dither_scale;
uniform float dither_scale_s;
uniform float dither_scale_t;

#include "varying/twoTexCompareVarying.hlsli"

// S24 (2026-08-02): see uiF.hlsl's comment - a bare Varying-struct PS input
// has no SV_Position field, so its first member starts at register 0
// while the VS's matching field (shifted by SV_Position's own real output
// register) is at register 1 - confirmed via fxc.exe disassembly.
struct PSInput
{
    float4 position : SV_Position;
    TwoTexCompareVarying varying;
};

float4 main(PSInput IN) : SV_Target
{
    float4 frag_color = abs(tex0.Sample(tex0Sampler, IN.varying.vary_texcoord0.xy) - tex1.Sample(tex1Sampler, IN.varying.vary_texcoord0.xy));

    float2 dither_coord;
    dither_coord[0] = IN.varying.vary_texcoord0[0] * dither_scale_s;
    dither_coord[1] = IN.varying.vary_texcoord0[1] * dither_scale_t;
    float4 dither_vec = dither_tex.Sample(dither_texSampler, dither_coord.xy);

    // Manually unrolled (was a dynamically-indexed for loop) - HLSL can't
    // natively address a vector component for writes via a non-constant
    // index, which forced the compiler to unroll this itself anyway
    // (warning X3550); writing it out avoids the warning with identical
    // behavior.
    if (frag_color[0] < dither_vec[0] * dither_scale) { frag_color[0] = 0.f; }
    if (frag_color[1] < dither_vec[1] * dither_scale) { frag_color[1] = 0.f; }
    if (frag_color[2] < dither_vec[2] * dither_scale) { frag_color[2] = 0.f; }

    return frag_color;
}
