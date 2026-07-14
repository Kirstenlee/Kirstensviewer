/**
 * @file class1/deferred/postDeferredGammaCorrect.hlsl
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

uniform float gamma;
uniform float2 screen_res;

float3 linear_to_srgb(float3 cl);

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float3 legacyGamma(float3 color)
{
    float3 c = 1.0 - clamp(color, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    c = 1.0 - pow(c, float3(gamma, gamma, gamma));
    return c;
}

float4 main(PSInput IN) : SV_Target
{
    float4 diff = diffuseRect.Sample(diffuseRectSampler, IN.vary_fragcoord);
    diff.rgb = linear_to_srgb(diff.rgb);
#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif
    diff.rgb = clamp(diff.rgb, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    return diff;
}
