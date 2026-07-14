/**
 * @file class3/deferred/screenSpaceReflFilterF.hlsl
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

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
uniform float2 pixelSize;
uniform int filterDir;
uniform float filterScale;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord.xy;

    const float weights[4] = { 0.214607, 0.189879, 0.131514, 0.071303 };

    float2 dir = (filterDir == 0) ? float2(pixelSize.x, 0.0) : float2(0.0, pixelSize.y);
    dir *= filterScale;

    float4 center = diffuseMap.Sample(diffuseMapSampler, tc);
    float w0 = weights[0] * center.a;

    float4 color = center * w0;
    float total_weight = w0;

    for (int i = 1; i < 4; i++)
    {
        float2 offset = dir * float(i);

        float4 s1 = diffuseMap.Sample(diffuseMapSampler, tc + offset);
        float w1 = weights[i] * s1.a;
        color += s1 * w1;
        total_weight += w1;

        float4 s2 = diffuseMap.Sample(diffuseMapSampler, tc - offset);
        float w2 = weights[i] * s2.a;
        color += s2 * w2;
        total_weight += w2;
    }

    return color / max(total_weight, 0.001);
}
