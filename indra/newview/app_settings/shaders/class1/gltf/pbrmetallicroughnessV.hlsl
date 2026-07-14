/**
 * @file class1/gltf/pbrmetallicroughnessV.hlsl
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

uniform float4x4 modelview_matrix;
uniform float4x4 projection_matrix;

uniform int gltf_material_id;

cbuffer GLTFMaterials : register(b0)
{
    float4 gltf_material_data[MAX_UBO_VEC4S];
};

float4 texture_base_color_transform[2];
float4 texture_normal_transform[2];
float4 texture_metallic_roughness_transform[2];
float4 texture_emissive_transform[2];
float4 texture_occlusion_transform[2];

void unpackTextureTransforms()
{
    if (gltf_material_id != -1)
    {
        int idx = gltf_material_id * 12;
        texture_base_color_transform[0] = gltf_material_data[idx + 0];
        texture_base_color_transform[1] = gltf_material_data[idx + 1];
        texture_normal_transform[0] = gltf_material_data[idx + 2];
        texture_normal_transform[1] = gltf_material_data[idx + 3];
        texture_metallic_roughness_transform[0] = gltf_material_data[idx + 4];
        texture_metallic_roughness_transform[1] = gltf_material_data[idx + 5];
        texture_emissive_transform[0] = gltf_material_data[idx + 6];
        texture_emissive_transform[1] = gltf_material_data[idx + 7];
        texture_occlusion_transform[0] = gltf_material_data[idx + 8];
        texture_occlusion_transform[1] = gltf_material_data[idx + 9];
    }
}

float2 khr_texture_transform(float2 texcoord, float2 scale, float rotation, float2 offset);
float2 texture_transform(float2 vertex_texcoord, float4[2] khr_gltf_transform, float4x4 sl_animation_transform);
float4 tangent_space_transform(float4 vertex_tangent, float3 vertex_normal, float4[2] khr_gltf_transform, float4x4 sl_animation_transform);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord0 : TEXCOORD0;
    float4 diffuse_color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float2 base_color_uv : TEXCOORD1;
    float2 emissive_uv : TEXCOORD2;
    float3 vary_normal : TEXCOORD3;
    float3 vary_tangent : TEXCOORD4;
    nointerpolation float vary_sign : TEXCOORD5;
    float2 normal_uv : TEXCOORD6;
    float2 metallic_roughness_uv : TEXCOORD7;
    float2 occlusion_uv : TEXCOORD8;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;
    unpackTextureTransforms();

    float4 pos = mul(modelview_matrix, float4(IN.position.xyz, 1.0));
    OUT.position = mul(projection_matrix, pos);
    OUT.vary_position = pos.xyz;

    float2 vertex_texcoord = IN.texcoord0;

    float4x4 identity = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    OUT.base_color_uv = texture_transform(vertex_texcoord, texture_base_color_transform, identity);
    OUT.emissive_uv = texture_transform(vertex_texcoord, texture_emissive_transform, identity);
    OUT.vertex_color = IN.diffuse_color;

    float3 n = normalize(mul((float3x3)modelview_matrix, IN.normal));
    float4 t = tangent_space_transform(IN.tangent, n, texture_normal_transform, identity);
    float3 tan = normalize(mul((float3x3)modelview_matrix, t.xyz));

    OUT.vary_normal = n;
    OUT.vary_tangent = tan;
    OUT.vary_sign = t.w;

    OUT.normal_uv = texture_transform(vertex_texcoord, texture_normal_transform, identity);
    OUT.metallic_roughness_uv = texture_transform(vertex_texcoord, texture_metallic_roughness_transform, identity);
    OUT.occlusion_uv = texture_transform(vertex_texcoord, texture_occlusion_transform, identity);

    return OUT;
}
