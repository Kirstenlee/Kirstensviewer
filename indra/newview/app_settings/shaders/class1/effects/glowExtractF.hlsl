/**
 * @file class1/effects/glowExtractF.hlsl
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

#if HAS_NOISE
Texture2D glowNoiseMap : register(t1);
SamplerState glowNoiseMapSampler : register(s1);
uniform float2 screen_res;
#endif

uniform float minLuminance;
uniform float maxExtractAlpha;
uniform float3 lumWeights;
uniform float3 warmthWeights;
uniform float warmthAmount;

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    float4 col = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);
    /// CALCULATING LUMINANCE (Using NTSC lum weights)
    float lum = smoothstep(minLuminance, minLuminance + 1.0, dot(col.rgb, lumWeights));
    float warmth = smoothstep(minLuminance, minLuminance + 1.0, max(col.r * warmthWeights.r, max(col.g * warmthWeights.g, col.b * warmthWeights.b)));

#if HAS_NOISE
    float TRUE_NOISE_RES = 128;
    float3 glow_noise = glowNoiseMap.Sample(glowNoiseMapSampler, IN.vary_texcoord0.xy * (screen_res / TRUE_NOISE_RES)).xyz;
    float NOISE_DEPTH = 64.0;
    col.rgb += glow_noise / NOISE_DEPTH;
    col.rgb = max(col.rgb, float3(0, 0, 0));
#endif

    float4 frag_color;
    frag_color.rgb = col.rgb;
    frag_color.a = max(col.a, lerp(lum, warmth, warmthAmount) * maxExtractAlpha);
    return frag_color;
}
