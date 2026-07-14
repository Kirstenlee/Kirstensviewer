/**
 * @file class1/deferred/gbufferUtil.hlsl
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

uniform Texture2D diffuseRect : register(t0);
uniform Texture2D specularRect : register(t1);
uniform SamplerState diffuseSampler : register(s0);
uniform SamplerState specularSampler : register(s1);

#if defined(HAS_EMISSIVE)
uniform Texture2D emissiveRect : register(t2);
uniform SamplerState emissiveSampler : register(s2);
#endif

float4 getNormRaw(float2 screenpos);
float4 decodeNormal(float4 norm);

struct GBufferInfo
{
    float4 albedo;
    float3 normal;
    float4 specular;
    float envIntensity;
    float gbufferFlag;
    float4 emissive;
};

#ifdef GET_GBUFFER_FLAG
#undef GET_GBUFFER_FLAG
#endif

GBufferInfo getGBuffer(float2 screenpos)
{
    GBufferInfo ret;
    float4 diffInfo = float4(0, 0, 0, 0);
    float4 specInfo = float4(0, 0, 0, 0);
    float4 emissInfo = float4(0, 0, 0, 0);

    diffInfo = diffuseRect.Sample(diffuseSampler, screenpos.xy);
    specInfo = specularRect.Sample(specularSampler, screenpos.xy);
    float4 normInfo = getNormRaw(screenpos);

#if defined(HAS_EMISSIVE)
    emissInfo = emissiveRect.Sample(emissiveSampler, screenpos.xy);
#endif

    ret.albedo = diffInfo;
    ret.normal = decodeNormal(normInfo).xyz;
    ret.specular = specInfo;
    ret.envIntensity = normInfo.b;
    ret.gbufferFlag = normInfo.w;
    ret.emissive = emissInfo;

    return ret;
}
