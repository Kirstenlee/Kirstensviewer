/**
 * @file class1/deferred/pbropaqueV.hlsl
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

//deferred opaque implementation

uniform float4x4 modelview_matrix;

#ifdef HAS_SKIN
uniform float4x4 projection_matrix;
float4x4 getObjectSkinnedTransform();
#else
uniform float3x3 normal_matrix;
uniform float4x4 modelview_projection_matrix;
#endif
uniform float4x4 texture_matrix0;

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
    float4 vertex_color : COLOR0;
    float3 vary_normal : TEXCOORD1;
    float3 vary_tangent : TEXCOORD2;
    nointerpolation float vary_sign : TEXCOORD3;
    float2 base_color_texcoord : TEXCOORD4;
    float2 normal_texcoord : TEXCOORD5;
    float2 metallic_roughness_texcoord : TEXCOORD6;
    float2 emissive_texcoord : TEXCOORD7;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

#ifdef HAS_SKIN
    float4x4 mat = getObjectSkinnedTransform();

    mat = mul(modelview_matrix, mat);

    float3 pos = mul(mat, float4(IN.position.xyz, 1.0)).xyz;
    OUT.vary_position = pos;
    OUT.position = mul(projection_matrix, float4(pos, 1.0));

#else
    OUT.vary_position = mul(modelview_matrix, float4(IN.position.xyz, 1.0)).xyz;
    //transform vertex
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
#endif

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
#endif

    n = normalize(n);

    float4 transformed_tangent = tangent_space_transform(float4(t, IN.tangent.w), n, texture_normal_transform, texture_matrix0);
    OUT.vary_tangent = normalize(transformed_tangent.xyz);
    OUT.vary_sign = transformed_tangent.w;
    OUT.vary_normal = n;

    OUT.vertex_color = IN.diffuse_color;

    return OUT;
}

#else

// fullbright HUD implementation

uniform float4x4 modelview_projection_matrix;

uniform float4x4 texture_matrix0;

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
    float4 vertex_color : COLOR0;
    float2 base_color_texcoord : TEXCOORD1;
    float2 emissive_texcoord : TEXCOORD2;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));

    // Not computed by the GLSL HUD path (no modelview_matrix uniform there);
    // kept here only to satisfy pbropaqueF.hlsl's PSInput interface.
    OUT.vary_position = float3(0, 0, 0);

    OUT.base_color_texcoord = texture_transform(IN.texcoord0, texture_base_color_transform, texture_matrix0);
    OUT.emissive_texcoord = texture_transform(IN.texcoord0, texture_emissive_transform, texture_matrix0);

    OUT.vertex_color = IN.diffuse_color;

    return OUT;
}

#endif
