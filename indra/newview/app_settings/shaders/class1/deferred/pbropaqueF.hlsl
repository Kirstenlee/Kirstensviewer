/**
 * @file class1/deferred/pbropaqueF.hlsl
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

#ifndef IS_HUD

// deferred opaque implementation

uniform Texture2D diffuseMap : register(t0);
uniform Texture2D bumpMap : register(t1);
uniform Texture2D emissiveMap : register(t2);
uniform Texture2D specularMap : register(t3);
uniform SamplerState diffuseMapSampler : register(s0);
uniform SamplerState bumpMapSampler : register(s1);
uniform SamplerState emissiveMapSampler : register(s2);
uniform SamplerState specularMapSampler : register(s3);

uniform float metallicFactor;
uniform float roughnessFactor;
uniform float3 emissiveColor;

struct PSInput
{
    float3 vary_position : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float3 vary_normal : TEXCOORD1;
    float3 vary_tangent : TEXCOORD2;
    nointerpolation float vary_sign : TEXCOORD3;
    float2 base_color_texcoord : TEXCOORD4;
    float2 normal_texcoord : TEXCOORD5;
    float2 metallic_roughness_texcoord : TEXCOORD6;
    float2 emissive_texcoord : TEXCOORD7;
};

struct PSOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 target3 : SV_Target3;
#endif
};

uniform float minimum_alpha;

float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

uniform float4 clipPlane;
uniform float clipSign;

void mirrorClip(float3 pos);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);

uniform float3x3 normal_matrix;

PSOutput main(PSInput IN)
{
    PSOutput OUT;
    mirrorClip(IN.vary_position);

    float4 basecolor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord.xy).rgba;
    basecolor.rgb = srgb_to_linear(basecolor.rgb);
    basecolor *= IN.vertex_color;

    if (basecolor.a < minimum_alpha)
        discard;

    float3 col = basecolor.rgb;

    float3 vNt = bumpMap.Sample(bumpMapSampler, IN.normal_texcoord.xy).xyz * 2.0 - 1.0;
    float sign = IN.vary_sign;
    float3 vN = IN.vary_normal;
    float3 vT = IN.vary_tangent.xyz;
    float3 vB = sign * cross(vN, vT);
    float3 tnorm = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);

    float3 spec = specularMap.Sample(specularMapSampler, IN.metallic_roughness_texcoord.xy).rgb;
    spec.g *= roughnessFactor;
    spec.b *= metallicFactor;

    float3 emissive = emissiveColor;
    emissive *= srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_texcoord.xy).rgb);

    tnorm *= IN.vary_sign;

    OUT.target0 = max(float4(col, 0.0), float4(0, 0, 0, 0));
    OUT.target1 = max(float4(spec.rgb, 0.0), float4(0, 0, 0, 0));
    OUT.target2 = encodeNormal(tnorm, 0, GBUFFER_FLAG_HAS_PBR);

#if defined(HAS_EMISSIVE)
    OUT.target3 = max(float4(emissive, 0), float4(0, 0, 0, 0));
#endif
    return OUT;
}

#else

// forward fullbright implementation for HUDs

uniform Texture2D diffuseMap : register(t0);
uniform Texture2D emissiveMap : register(t1);
uniform SamplerState diffuseMapSampler : register(s0);
uniform SamplerState emissiveMapSampler : register(s1);

uniform float3 emissiveColor;

struct PSInput
{
    float3 vary_position : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float2 base_color_texcoord : TEXCOORD1;
    float2 emissive_texcoord : TEXCOORD2;
};

uniform float minimum_alpha;

float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

float4 main(PSInput IN) : SV_Target
{
    float4 basecolor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord.xy).rgba;
    basecolor.a *= IN.vertex_color.a;
    if (basecolor.a < minimum_alpha)
        discard;

    float3 col = IN.vertex_color.rgb * srgb_to_linear(basecolor.rgb);
    float3 emissive = emissiveColor;
    emissive *= srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_texcoord.xy).rgb);
    col += emissive;

    return float4(linear_to_srgb(col), 0.0);
}

#endif
