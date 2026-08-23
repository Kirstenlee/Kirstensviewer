/**
 * @file class3/deferred/screenSpaceReflAlphaF.hlsl
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

// S24 (2026-08-19, task #236, task #227 audit finding): this file was a
// byte-for-byte copy of screenSpaceReflTraceF.hlsl (the G-buffer fullscreen-
// pass pattern: PSInput{vary_fragcoord, camera_ray}, getGBuffer()/getDepth()/
// getPositionWithDepth()), not a port of its own screenSpaceReflAlphaF.glsl
// at all - which is a forward-shaded, per-vertex-input alpha/GLTF pass
// (vary_position/vary_normal/base_color_texcoord/etc, matching this file's
// own paired screenSpaceReflAlphaV.hlsl, never actually read by the old
// body). screenSpaceReflUtil.hlsl's own comment confirms this whole program
// group (screenSpaceReflAlphaF/TraceF/FilterF/PostV/F/WaterF's SSR-alpha
// variant) is currently unreferenced by any registered shader program - so
// this had zero live effect, but was still a genuinely wrong port. Real
// body ported below, faithfully.
Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D specularMap : register(t1);
SamplerState specularMapSampler : register(s1);
Texture2D sceneMap : register(t2);
SamplerState sceneMapSampler : register(s2);

uniform float4x4 projection_matrix;
uniform float roughnessFactor;
uniform float minimum_alpha;

float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness);
void bayerDitherDiscard(float alpha, float threshold, float4 fragCoord);

struct PSInput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float3 vary_normal : TEXCOORD1;
    float2 base_color_texcoord : TEXCOORD2;
    float2 metallic_roughness_texcoord : TEXCOORD3;
    float4 vertex_color : COLOR0;
};

float4 main(PSInput IN) : SV_Target
{
    float4 baseColor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord);
    float alpha = baseColor.a * IN.vertex_color.a;

    // Alpha mask test
    if (minimum_alpha >= 0.0 && alpha < minimum_alpha)
        discard;

    bayerDitherDiscard(alpha, 1.0, IN.position);

    // Per-pixel roughness from ORM green channel, scaled by material factor
    float roughness = specularMap.Sample(specularMapSampler, IN.metallic_roughness_texcoord).g * roughnessFactor;
    float glossiness = 1.0 - roughness;

    // Derive tc from view-space position via projection rather than
    // gl_FragCoord/screen_res - the SSR buffer may be at reduced resolution.
    float4 projPos = mul(projection_matrix, float4(IN.vary_position, 1.0));
    float2 tc = (projPos.xy / projPos.w) * 0.5 + 0.5;
    float3 norm = normalize(IN.vary_normal);

    float4 ssrColor = float4(0.0, 0.0, 0.0, 0.0);
    tapScreenSpaceReflection(1, tc, IN.vary_position, norm, ssrColor, sceneMap, sceneMapSampler, glossiness);
    float4 frag_color = ssrColor;
    frag_color.a *= alpha;
    return frag_color;
}
