/**
 * @file class1/deferred/pbrglowV.hlsl
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

#ifdef HAS_SKIN
uniform float4x4 modelview_matrix;
uniform float4x4 projection_matrix;
float4x4 getObjectSkinnedTransform();
#else
uniform float4x4 modelview_projection_matrix;
#endif

uniform float4x4 texture_matrix0;

uniform float4 texture_base_color_transform[2];
uniform float4 texture_emissive_transform[2];

float2 texture_transform(float2 vertex_texcoord, float4 khr_gltf_transform[2], float4x4 sl_animation_transform);

struct VSInput
{
    float3 position : POSITION;
    float4 emissive : COLOR0;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 base_color_texcoord : TEXCOORD0;
    float2 emissive_texcoord : TEXCOORD1;
    float4 vertex_emissive : COLOR0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

#ifdef HAS_SKIN
    float4x4 mat = getObjectSkinnedTransform();

    mat = mul(modelview_matrix, mat);

    float3 pos = mul(mat, float4(IN.position.xyz, 1.0)).xyz;

    OUT.position = mul(projection_matrix, float4(pos, 1.0));
#else
    //transform vertex
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
#endif

    OUT.base_color_texcoord = texture_transform(IN.texcoord0, texture_base_color_transform, texture_matrix0);
    OUT.emissive_texcoord = texture_transform(IN.texcoord0, texture_emissive_transform, texture_matrix0);

    OUT.vertex_emissive = IN.emissive;

    return OUT;
}
