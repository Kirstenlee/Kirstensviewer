/**
 * @file class3/deferred/screenSpaceReflPostF.hlsl
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

// screen_res/inv_proj are also declared (and used) by deferredUtil.hlsl,
// attached below - deleted here rather than guarded since unused in this file.
//
// projection_matrix is renamed (not deleted) below: screenSpaceReflUtil.hlsl
// (also attached whenever hasScreenSpaceReflections is set) declares its own
// unguarded `projection_matrix` uniform, and two unguarded declarations of
// the same name in one concatenated HLSL compile is a hard error (X3003),
// unlike GLSL's separate-compile-then-link model. Unused in this file's own
// code, so renaming is a safe minimal fix.
uniform float4x4 unused_dead_projection_matrix;
uniform float zNear;
uniform float zFar;

#include "varying/screenSpaceReflPostVarying.hlsli"

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (attached below) - moved this
// file's own three textures to t4-t6/s4-s6 (this shader has no
// gbufferUtil.hlsl/reflectionProbeF.hlsl attached - hasFullGBuffer/
// hasReflectionProbes confirmed not set - so t4-t9 were all free).
Texture2D specularRect : register(t4);
SamplerState specularRectSampler : register(s4);
Texture2D diffuseRect : register(t5);
SamplerState diffuseRectSampler : register(s5);
Texture2D diffuseMap : register(t6);
SamplerState diffuseMapSampler : register(s6);

float4 getNorm(float2 screenpos);
float getDepth(float2 pos_screen);
float linearDepth(float d, float znear, float zfar);
float linearDepth01(float d, float znear, float zfar);

float4 getPositionWithDepth(float2 pos_screen, float depth);
float4 getPosition(float2 pos_screen);

float random (float2 uv);

float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness);

// SV_Position must be declared first in PSInput to match VS output register order - see uiF.hlsl.
struct PSInput
{
    float4 position : SV_Position;
    ScreenSpaceReflPostVarying varying;
};

float4 main(PSInput IN) : SV_Target
{
    float2  tc = IN.varying.vary_fragcoord.xy;
    float depth = linearDepth01(getDepth(tc), zNear, zFar);
    float4 norm = getNorm(tc); // need `norm.w` for GET_GBUFFER_FLAG()
    float3 pos = getPositionWithDepth(tc, getDepth(tc)).xyz;
    // GL-vs-D3D11 texture-origin flip inlined at each direct .Sample() call;
    // tc itself stays unflipped since getDepth()/getNorm()/getPositionWithDepth()
    // above already apply their own internal flip and expect raw input.
    float4 spec    = specularRect.Sample(specularRectSampler, float2(tc.x, 1.0 - tc.y));
    float2 hitpixel;

    float4 diffuse = diffuseRect.Sample(diffuseRectSampler, float2(tc.x, 1.0 - tc.y));
    float3 specCol = spec.rgb;

    float4 fcol = diffuseMap.Sample(diffuseMapSampler, float2(tc.x, 1.0 - tc.y));

    if (GET_GBUFFER_FLAG(norm.w, GBUFFER_FLAG_HAS_PBR))
    {
        float3 orm = specCol.rgb;
        float perceptualRoughness = orm.g;
        float metallic = orm.b;
        float3 f0 = float3(0.04, 0.04, 0.04);
        float3 baseColor = diffuse.rgb;

        float3 diffuseColor = baseColor.rgb*(float3(1.0, 1.0, 1.0)-f0);

        specCol = lerp(f0, baseColor.rgb, metallic);
    }

    float4 collectedColor = float4(0, 0, 0, 0);

    float w = tapScreenSpaceReflection(4, tc, pos, norm.xyz, collectedColor, diffuseMap, diffuseMapSampler, 0.f);

    collectedColor.rgb *= specCol.rgb;

    fcol += collectedColor * w;
    return max(fcol, float4(0, 0, 0, 0));
}
