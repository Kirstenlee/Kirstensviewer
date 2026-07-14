/**
 * @file class1/deferred/pbrShadowAlphaMaskV.hlsl
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

uniform float4x4 texture_matrix0;
#if defined(HAS_SKIN)
uniform float4x4 modelview_matrix;
uniform float4x4 projection_matrix;
float4x4 getObjectSkinnedTransform();
#else
uniform float4x4 modelview_projection_matrix;
#endif

uniform float4 texture_base_color_transform[2];
float2 texture_transform(float2 vertex_texcoord, float4 khr_gltf_transform[2], float4x4 sl_animation_transform);

uniform float shadow_target_width;

void passTextureIndex();

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 post_pos : TEXCOORD0;
    float target_pos_x : TEXCOORD1;
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD2;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
#if defined(HAS_SKIN)
    float4 pre_pos = float4(IN.position.xyz, 1.0);
    float4x4 mat = getObjectSkinnedTransform();
    mat = mul(modelview_matrix, mat);
    float4 pos = mul(mat, pre_pos);
    pos = mul(projection_matrix, pos);
#else
    float4 pre_pos = float4(IN.position.xyz, 1.0);
    float4 pos = mul(modelview_projection_matrix, pre_pos);
#endif

    OUT.target_pos_x = 0.5 * (shadow_target_width - 1.0) * pos.x;

    OUT.post_pos = pos;

    OUT.position = pos;

    passTextureIndex();

    OUT.vary_texcoord0 = texture_transform(IN.texcoord0, texture_base_color_transform, texture_matrix0);
    OUT.vertex_color = IN.diffuse_color;

    return OUT;
}
