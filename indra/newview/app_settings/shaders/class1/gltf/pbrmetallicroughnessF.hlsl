/**
 * @file class1/gltf/pbrmetallicroughnessF.hlsl
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

// GLTF pbrMetallicRoughness implementation

uniform int gltf_material_id;

float3 emissiveColor = float3(0,0,0);
float metallicFactor = 1.0;
float roughnessFactor = 1.0;
float minimum_alpha = -1.0;

cbuffer GLTFMaterials : register(b0)
{
    float4 gltf_material_data[MAX_UBO_VEC4S];
};

void unpackMaterial()
{
    if (gltf_material_id > -1)
    {
        int idx = gltf_material_id * 12;
        emissiveColor = gltf_material_data[idx + 10].rgb;
        roughnessFactor = gltf_material_data[idx + 11].g;
        metallicFactor = gltf_material_data[idx + 11].b;
        minimum_alpha -= gltf_material_data[idx + 11].a;
    }
}

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D emissiveMap : register(t1);
SamplerState emissiveMapSampler : register(s1);

void mirrorClip(float3 pos);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);
float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

#ifndef UNLIT
Texture2D normalMap : register(t2);
SamplerState normalMapSampler : register(s2);
Texture2D metallicRoughnessMap : register(t3);
SamplerState metallicRoughnessMapSampler : register(s3);
Texture2D occlusionMap : register(t4);
SamplerState occlusionMapSampler : register(s4);
#endif

struct PSInput
{
    float3 vary_position : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float2 base_color_uv : TEXCOORD1;
    float2 emissive_uv : TEXCOORD2;
#ifndef UNLIT
    float3 vary_normal : TEXCOORD3;
    float3 vary_tangent : TEXCOORD4;
    nointerpolation float vary_sign : TEXCOORD5;
    float2 normal_uv : TEXCOORD6;
    float2 metallic_roughness_uv : TEXCOORD7;
    float2 occlusion_uv : TEXCOORD8;
#endif
};

#ifdef UNLIT
float4 main(PSInput IN) : SV_Target
{
    unpackMaterial();

    float4 baseColor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_uv);
    baseColor.rgb = srgb_to_linear(baseColor.rgb);
    baseColor *= IN.vertex_color;

    if (baseColor.a < minimum_alpha)
    {
        discard;
    }

    float4 color = baseColor;
    color.rgb += emissiveColor * srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_uv).rgb);

    float4 frag_color = max(color, float4(0, 0, 0, 0));
    return frag_color;
}
#else
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
    unpackMaterial();
    mirrorClip(IN.vary_position);

    float4 baseColor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_uv);
    baseColor.rgb = srgb_to_linear(baseColor.rgb);
    baseColor *= IN.vertex_color;

    if (baseColor.a < minimum_alpha)
    {
        discard;
    }

    float3 n = normalize(IN.vary_normal);
    float3 t = normalize(IN.vary_tangent);
    float3 b = IN.vary_sign * cross(n, t);
    float3 tnorm = normalize(normalMap.Sample(normalMapSampler, IN.normal_uv).xyz * 2.0 - 1.0);
    tnorm = normalize(tnorm.x * t + tnorm.y * b + tnorm.z * n);

    float3 orm = metallicRoughnessMap.Sample(metallicRoughnessMapSampler, IN.metallic_roughness_uv).rgb;
    float occlusion = occlusionMap.Sample(occlusionMapSampler, IN.occlusion_uv).r;
    float perceptualRoughness = orm.g * roughnessFactor;
    float metallic = orm.b * metallicFactor;

    float3 emissive = emissiveColor * srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_uv).rgb);

    float3 col = baseColor.rgb;

    OUT.data0 = max(float4(col, 0.0), float4(0, 0, 0, 0));
    OUT.data1 = max(float4(occlusion, perceptualRoughness, metallic, 0.0), float4(0, 0, 0, 0));
    OUT.data2 = encodeNormal(tnorm, 0, GBUFFER_FLAG_HAS_PBR);

#if defined(HAS_EMISSIVE)
    OUT.data3 = max(float4(emissive, 0), float4(0, 0, 0, 0));
#endif
    return OUT;
}
#endif
