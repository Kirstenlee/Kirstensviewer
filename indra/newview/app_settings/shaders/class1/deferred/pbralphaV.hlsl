/**
 * @file class1/deferred/pbralphaV.hlsl
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

#ifndef IS_HUD

// default alpha implementation

#ifdef HAS_SKIN
uniform float4x4 modelview_matrix;
uniform float4x4 projection_matrix;
float4x4 getObjectSkinnedTransform();
#else
uniform float3x3 normal_matrix;
uniform float4x4 modelview_projection_matrix;
#endif
uniform float4x4 texture_matrix0;

#if !defined(HAS_SKIN)
uniform float4x4 modelview_matrix;
#endif

uniform float4 texture_base_color_transform[2];
uniform float4 texture_normal_transform[2];
uniform float4 texture_metallic_roughness_transform[2];
uniform float4 texture_emissive_transform[2];

float2 texture_transform(float2 vertex_texcoord, float4 khr_gltf_transform[2], float4x4 sl_animation_transform);
float4 tangent_space_transform(float4 vertex_tangent, float3 vertex_normal, float4 khr_gltf_transform[2], float4x4 sl_animation_transform);

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float3 vary_fragcoord : TEXCOORD1;
    float2 base_color_texcoord : TEXCOORD2;
    float2 normal_texcoord : TEXCOORD3;
    float2 metallic_roughness_texcoord : TEXCOORD4;
    float2 emissive_texcoord : TEXCOORD5;
    float4 vertex_color : COLOR0;
    float3 vary_tangent : TEXCOORD6;
    nointerpolation float vary_sign : TEXCOORD7;
    float3 vary_normal : TEXCOORD8;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

#ifdef HAS_SKIN
    float4x4 mat = getObjectSkinnedTransform();
    mat = mul(modelview_matrix, mat);
    float3 pos = mul(mat, float4(IN.position.xyz, 1.0)).xyz;
    OUT.vary_position = pos;
    float4 vert = mul(projection_matrix, float4(pos, 1.0));
#else
    //transform vertex
    float4 vert = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
#endif
    OUT.position = vert;

    OUT.vary_fragcoord = vert.xyz;

    OUT.base_color_texcoord = texture_transform(IN.texcoord0, texture_base_color_transform, texture_matrix0);
    OUT.normal_texcoord = texture_transform(IN.texcoord0, texture_normal_transform, texture_matrix0);
    OUT.metallic_roughness_texcoord = texture_transform(IN.texcoord0, texture_metallic_roughness_transform, texture_matrix0);
    OUT.emissive_texcoord = texture_transform(IN.texcoord0, texture_emissive_transform, texture_matrix0);

#ifdef HAS_SKIN
    float3 n = mul(mat, float4(IN.normal.xyz + IN.position.xyz, 1.0)).xyz - pos.xyz;
    float3 t = mul(mat, float4(IN.tangent.xyz + IN.position.xyz, 1.0)).xyz - pos.xyz;
#else //HAS_SKIN
    float3 n = mul(normal_matrix, IN.normal);
    float3 t = mul(normal_matrix, IN.tangent.xyz);
#endif //HAS_SKIN

    n = normalize(n);

    float4 transformed_tangent = tangent_space_transform(float4(t, IN.tangent.w), n, texture_normal_transform, texture_matrix0);
    OUT.vary_tangent = normalize(transformed_tangent.xyz);
    OUT.vary_sign = transformed_tangent.w;
    OUT.vary_normal = n;

    OUT.vertex_color = IN.diffuse_color;

#if !defined(HAS_SKIN)
    OUT.vary_position = mul(modelview_matrix, float4(IN.position.xyz, 1.0)).xyz;
#endif

    return OUT;
}

#else

// fullbright HUD alpha implementation

uniform float4x4 modelview_projection_matrix;

uniform float4x4 texture_matrix0;

uniform float4x4 modelview_matrix;

uniform float4 texture_base_color_transform[2];
uniform float4 texture_emissive_transform[2];

float2 texture_transform(float2 vertex_texcoord, float4 khr_gltf_transform[2], float4x4 sl_animation_transform);

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float2 base_color_texcoord : TEXCOORD1;
    float2 emissive_texcoord : TEXCOORD2;
    float4 vertex_color : COLOR0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    float4 vert = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
    OUT.position = vert;
    OUT.vary_position = vert.xyz;

    OUT.base_color_texcoord = texture_transform(IN.texcoord0, texture_base_color_transform, texture_matrix0);
    OUT.emissive_texcoord = texture_transform(IN.texcoord0, texture_emissive_transform, texture_matrix0);

    OUT.vertex_color = IN.diffuse_color;

    return OUT;
}

#endif
