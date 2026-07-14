/**
 * @file class3/deferred/screenSpaceReflTraceF.hlsl
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

Texture2D sceneMap : register(t0);
SamplerState sceneMapSampler : register(s0);

GBufferInfo getGBuffer(float2 screenpos);
float getDepth(float2 pos_screen);
float4 getPositionWithDepth(float2 pos_screen, float depth);
float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness);

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
    float3 camera_ray : TEXCOORD1;
};

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord.xy;
    float depth = getDepth(tc);
    if (depth >= 1.0) return float4(0.0, 0.0, 0.0, 0.0);

    GBufferInfo gb = getGBuffer(tc);
    float3 pos = getPositionWithDepth(tc, depth).xyz;
    float glossiness = GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_PBR) ? (1.0 - gb.specular.g) : gb.specular.a;

    float4 ssrColor = float4(0.0, 0.0, 0.0, 0.0);
    tapScreenSpaceReflection(1, tc, pos, gb.normal, ssrColor, sceneMap, sceneMapSampler, glossiness);
    return ssrColor;
}
