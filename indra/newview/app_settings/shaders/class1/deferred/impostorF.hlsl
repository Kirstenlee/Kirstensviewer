/**
 * @file class1/deferred/impostorF.hlsl
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

uniform float minimum_alpha;

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D normalMap : register(t1);
SamplerState normalMapSampler : register(s1);
Texture2D specularMap : register(t2);
SamplerState specularMapSampler : register(s2);

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
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

float3 linear_to_srgb(float3 c);

float4 encodeNormal(float3 n, float env, float gbuffer_flag);

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    float4 col = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);

    if (col.a < minimum_alpha)
    {
        discard;
    }

    float4 norm = normalMap.Sample(normalMapSampler,   IN.vary_texcoord0.xy);
    float4 spec = specularMap.Sample(specularMapSampler, IN.vary_texcoord0.xy);

    OUT.data0 = float4(col.rgb, 0.0);
    OUT.data1 = spec;
    OUT.data2 = float4(norm.xyz, GBUFFER_FLAG_HAS_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data3 = float4(0, 0, 0, 0);
#endif

    return OUT;
}
