/**
 * @file class1/deferred/luminanceF.hlsl
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

// take a luminance sample of diffuseRect and emissiveRect

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);
Texture2D emissiveRect : register(t1);
SamplerState emissiveRectSampler : register(s1);
Texture2D normalMap : register(t2);
SamplerState normalMapSampler : register(s2);
uniform float diffuse_luminance_scale;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float lum(float3 col)
{
    float3 l = float3(0.2126, 0.7152, 0.0722);
    return dot(l, col);
}

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord*0.6+0.2;
    tc.y -= 0.1; // HACK - nudge exposure sample down a little bit to favor ground over sky
    float3 c = diffuseRect.Sample(diffuseRectSampler, tc).rgb;

    float4  norm         = normalMap.Sample(normalMapSampler, tc);

    if (!GET_GBUFFER_FLAG(norm.w, GBUFFER_FLAG_HAS_HDRI) &&
        !GET_GBUFFER_FLAG(norm.w, GBUFFER_FLAG_SKIP_ATMOS))
    {
        // Apply the diffuse luminance scale to objects but not the sky
        // Prevents underexposing when looking at bright environments
        // while still allowing for realistically bright skies.
        c *= diffuse_luminance_scale;
    }

    c += emissiveRect.Sample(emissiveRectSampler, tc).rgb;

    float L = lum(c);
    float m = max(L, 0.0);
    return float4(m, m, m, m);
}
