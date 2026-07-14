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

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);
Texture2D depthMap : register(t1);
SamplerState depthMapSampler : register(s1);

uniform float2 screen_res;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float hash(float n) { return frac(sin(n) * 1e4); }
float hash(float2 p) { return frac(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }

float noise(float x) {
    float i = floor(x);
    float f = frac(x);
    float u = f * f * (3.0 - 2.0 * f);
    return lerp(hash(i), hash(i + 1.0), u);
}

float4 main(PSInput IN) : SV_Target
{
    float4 result = float4(0, 0, 0, 0);
    float weight = 0;
    float depth = depthMap.SampleLevel(depthMapSampler, IN.vary_fragcoord, 0).r;
    float2 jitter = float2(hash(IN.vary_fragcoord + 0.5), hash(IN.vary_fragcoord * 2.0 + 1.5)) - 0.5;

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 offset = float2((i % 3) - 1 + jitter.x * 0.5, (i / 3) - 1 + jitter.y * 0.5) / screen_res;
        float4 sample_color = diffuseRect.SampleLevel(diffuseRectSampler, IN.vary_fragcoord + offset, 0);
        float sample_weight = 1.0;
        float sample_depth = depthMap.SampleLevel(depthMapSampler, IN.vary_fragcoord + offset, 0).r;
        sample_weight *= exp(-abs(sample_depth - depth) * 100.0);
        result += sample_color * sample_weight;
        weight += sample_weight;
    }
    if (weight > 0.0) result /= weight;
    else result = diffuseRect.SampleLevel(diffuseRectSampler, IN.vary_fragcoord, 0);
    result.rgb = clamp(result.rgb, 0.0, 1.0);
    return result;
}
