/**
 * @file class3/deferred/ssrF.hlsl
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

// Screen-space reflection pass for opaque deferred surfaces. Runs before deferred lighting,
// reads the gbuffer and the previous frame's lit scene (mSceneMap), and writes the traced
// reflection to the SSR buffer. softenLightF.hlsl then reads that buffer in place of tracing
// inline. Forward and transparent paths keep their own inline trace (same function).
//
// The gloss choice and gates match softenLightF.hlsl's material branches:
//   PBR surfaces:    gloss = 1 - roughness, traced only when gloss >= ssrGlossThreshold
//   legacy surfaces: gloss = specular.a, no threshold
//   HDRI and sky:    no probe lighting, nothing traced
//
// The loader places this body before its attached helpers (deferredUtil, gbufferUtil,
// screenSpaceReflUtil), so everything main() calls is declared here first, the way
// softenLightF.hlsl does. The guards match the helpers' own, so whichever copy arrives first wins.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif

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

float getDepth(float2 pos_screen);
float4 getPositionWithDepth(float2 pos_screen, float depth);
GBufferInfo getGBuffer(float2 screenpos);
float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness);

Texture2D sceneMap : register(t19);
uniform SamplerState sceneMapSampler : register(s9);

uniform float ssrGlossThreshold;

struct PSInput
{
    // SV_Position required here - its absence shifts every interpolant register (see uiF.hlsl).
    float4 position : SV_Position;
    float2 vary_texcoord0 : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    // Same GL-convention tc as softenLightF.hlsl's vary_fragcoord (y up). getDepth() and
    // getGBuffer() flip it at their own sample calls, so pixel row = (1 - tc.y) * height.
    float2 tc = float2(IN.position.x / screen_res.x, 1.0 - IN.position.y / screen_res.y);

    float depth = getDepth(tc);
    float4 pos = getPositionWithDepth(tc, depth);
    GBufferInfo gb = getGBuffer(tc);

    float4 ssr = float4(0, 0, 0, 0);

    if (GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_PBR))
    {
        float gloss = 1.0 - gb.specular.g; // specular.g is perceptual roughness
        if (gloss >= ssrGlossThreshold)
        {
            tapScreenSpaceReflection(1, tc, pos.xyz, gb.normal, ssr, sceneMap, sceneMapSampler, gloss);
        }
    }
    else if (!GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_HDRI) &&
             !GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_SKIP_ATMOS))
    {
        tapScreenSpaceReflection(1, tc, pos.xyz, gb.normal, ssr, sceneMap, sceneMapSampler, gb.specular.a);
    }

    return ssr;
}
