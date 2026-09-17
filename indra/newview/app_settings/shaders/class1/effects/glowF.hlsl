/**
 * @file class1/effects/glowF.hlsl
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

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

uniform float glowStrength;

#include "varying/glowVarying.hlsli"

// SV_Position required on bare-Varying PS inputs - see uiF.hlsl's comment.
struct PSInput
{
    float4 position : SV_Position;
    GlowVarying varying;
};

// GL-vs-D3D11 texture-origin flip, same fix as glowExtractF.hlsl. Safe to
// flip each of the 8 taps' V component independently: the kernel weights
// are mirror-symmetric (kern[0]==kern[7], kern[1]==kern[6], etc.), so
// inverting the offset direction doesn't change the weighted sum.
float2 flipV(float2 tc) { return float2(tc.x, 1.0 - tc.y); }

float4 main(PSInput IN) : SV_Target
{
    float4 col = float4(0.0, 0.0, 0.0, 0.0);

    float kern[8];
    kern[0] = 0.25; kern[1] = 0.5; kern[2] = 0.8; kern[3] = 1.0;
    kern[4] = 1.0;  kern[5] = 0.8; kern[6] = 0.5; kern[7] = 0.25;

    col += kern[0] * diffuseMap.Sample(diffuseMapSampler, flipV(IN.varying.vary_texcoord0.xy));
    col += kern[1] * diffuseMap.Sample(diffuseMapSampler, flipV(IN.varying.vary_texcoord1.xy));
    col += kern[2] * diffuseMap.Sample(diffuseMapSampler, flipV(IN.varying.vary_texcoord2.xy));
    col += kern[3] * diffuseMap.Sample(diffuseMapSampler, flipV(IN.varying.vary_texcoord3.xy));
    col += kern[4] * diffuseMap.Sample(diffuseMapSampler, flipV(IN.varying.vary_texcoord0.zw));
    col += kern[5] * diffuseMap.Sample(diffuseMapSampler, flipV(IN.varying.vary_texcoord1.zw));
    col += kern[6] * diffuseMap.Sample(diffuseMapSampler, flipV(IN.varying.vary_texcoord2.zw));
    col += kern[7] * diffuseMap.Sample(diffuseMapSampler, flipV(IN.varying.vary_texcoord3.zw));

    return max(float4(col.rgb * glowStrength, col.a), float4(0, 0, 0, 0));
}
