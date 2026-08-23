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

// t0-t3/s0-s3 are reserved by deferredUtil.hlsl's normalMap/depthMap/
// projectionMap/brdfLut - both files are attached together whenever a
// shader sets isDeferred+hasFullGBuffer (e.g. the whole pointLightF/
// multiPointLightF/spotLightF/softenLightF family), a combination never
// successfully compiled before "Deferred Light Shader" first reached this
// far, which is why this collision stayed latent until now. Moved to
// t4-t6/s4-s6 - t10-t15/s10-s15 are separately reserved by
// shadowUtil.hlsl's shadowMap0-5.
uniform Texture2D diffuseRect : register(t4);
uniform Texture2D specularRect : register(t5);
uniform SamplerState diffuseSampler : register(s4);
uniform SamplerState specularSampler : register(s5);

#if defined(HAS_EMISSIVE)
uniform Texture2D emissiveRect : register(t6);
uniform SamplerState emissiveSampler : register(s6);
#endif

float4 getNormRaw(float2 screenpos);
float4 decodeNormal(float4 norm);

// Also declared, identically, by entry files that reference GBufferInfo
// in their own forward declarations before this (attached) file's text
// arrives in the concatenated source - see e.g. pointLightF.hlsl's
// comment. Include guarded so whichever copy concatenates first wins.
#ifndef LL_GBUFFERINFO_DECLARED
#define LL_GBUFFERINFO_DECLARED
struct GBufferInfo
{
    float4 albedo;
    float3 normal;
    float4 specular;
    float envIntensity;
    float gbufferFlag;
    float4 emissive;
};
#endif

#ifdef GET_GBUFFER_FLAG
#undef GET_GBUFFER_FLAG
#endif

GBufferInfo getGBuffer(float2 screenpos)
{
    GBufferInfo ret;
    float4 diffInfo = float4(0, 0, 0, 0);
    float4 specInfo = float4(0, 0, 0, 0);
    float4 emissInfo = float4(0, 0, 0, 0);

    // S24 (2026-08-04): GL's texture origin is bottom-left, D3D11's is
    // top-left - flip here, at the actual texture reads, not in screenpos
    // itself (screenpos/vary_fragcoord is also used elsewhere - e.g.
    // getPositionWithDepth()'s inverse-projection math - which must stay
    // in the camera's own NDC convention and must NOT be flipped; see
    // getNorm()/getDepth() in deferredUtil.hlsl for the same fix and
    // fuller explanation). getNormRaw() already does its own flip
    // internally, so it's called with the unflipped screenpos here.
    float2 flipped = float2(screenpos.x, 1.0 - screenpos.y);
    diffInfo = diffuseRect.Sample(diffuseSampler, flipped);
    specInfo = specularRect.Sample(specularSampler, flipped);
    float4 normInfo = getNormRaw(screenpos);

#if defined(HAS_EMISSIVE)
    emissInfo = emissiveRect.Sample(emissiveSampler, flipped);
#endif

    ret.albedo = diffInfo;
    ret.normal = decodeNormal(normInfo).xyz;
    ret.specular = specInfo;
    ret.envIntensity = normInfo.b;
    ret.gbufferFlag = normInfo.w;
    ret.emissive = emissInfo;

    return ret;
}
