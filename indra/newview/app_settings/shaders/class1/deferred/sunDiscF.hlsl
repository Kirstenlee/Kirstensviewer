/**
 * @file class1/deferred/sunDiscF.hlsl
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

float3 srgb_to_linear(float3 c);

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D altDiffuseMap : register(t1);
SamplerState altDiffuseMapSampler : register(s1);
uniform float blend_factor; // interp factor between sunDisc A/B

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
    float sun_fade : TEXCOORD1;
};

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    float4 sunDiscA = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);
    float4 sunDiscB = altDiffuseMap.Sample(altDiffuseMapSampler, IN.vary_texcoord0.xy);
    float4 c     = lerp(sunDiscA, sunDiscB, blend_factor);


    // SL-9806 stars poke through
    //c.a *= sun_fade;

    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    OUT.data2 = float4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);
#if defined(HAS_EMISSIVE)
    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data3 = c;
#else
    OUT.data0 = c;
#endif

    return OUT;
}
