/**
 * @file class1/deferred/terrainF.hlsl
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

Texture2D detail_0 : register(t0);
SamplerState detail_0Sampler : register(s0);
Texture2D detail_1 : register(t1);
SamplerState detail_1Sampler : register(s1);
Texture2D detail_2 : register(t2);
SamplerState detail_2Sampler : register(s2);
Texture2D detail_3 : register(t3);
SamplerState detail_3Sampler : register(s3);
Texture2D alpha_ramp : register(t4);
SamplerState alpha_rampSampler : register(s4);

void mirrorClip(float3 position);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);

struct PSInput
{
    float3 pos : TEXCOORD0;
    float3 vary_normal : TEXCOORD1;
    float4 vary_texcoord0 : TEXCOORD2;
    float4 vary_texcoord1 : TEXCOORD3;
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
    mirrorClip(IN.pos);

    float4 color0 = detail_0.Sample(detail_0Sampler, IN.vary_texcoord0.xy);
    float4 color1 = detail_1.Sample(detail_1Sampler, IN.vary_texcoord0.xy);
    float4 color2 = detail_2.Sample(detail_2Sampler, IN.vary_texcoord0.xy);
    float4 color3 = detail_3.Sample(detail_3Sampler, IN.vary_texcoord0.xy);

    float alpha1 = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord0.zw).a;
    float alpha2 = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord1.xy).a;
    float alphaFinal = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord1.zw).a;
    float4 outColor = lerp(lerp(color3, color2, alpha2), lerp(color1, color0, alpha1), alphaFinal);

    outColor.a = 0.0;

    OUT.data0 = max(outColor, float4(0, 0, 0, 0));
    OUT.data1 = float4(0.0, 0.0, 0.0, -1.0);
    float3 nvn = normalize(IN.vary_normal);
    OUT.data2 = encodeNormal(nvn.xyz, 0, GBUFFER_FLAG_HAS_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data3 = float4(0, 0, 0, 0);
#endif
    return OUT;
}
