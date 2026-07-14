/**
 * @file class1/deferred/avatarAlphaShadowV.hlsl
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
uniform float4x4 projection_matrix;
uniform float shadow_target_width;

float4x4 getSkinnedTransform();
void passTextureIndex();

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float pos_w : TEXCOORD0;
    float target_pos_x : TEXCOORD1;
    float2 vary_texcoord0 : TEXCOORD2;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    float4 pos;
    float3 norm;

    float4 pos_in = float4(IN.position.xyz, 1.0);
    float4x4 trans = getSkinnedTransform();
    pos.x = dot(trans[0], pos_in);
    pos.y = dot(trans[1], pos_in);
    pos.z = dot(trans[2], pos_in);
    pos.w = 1.0;

    norm.x = dot(trans[0].xyz, IN.normal);
    norm.y = dot(trans[1].xyz, IN.normal);
    norm.z = dot(trans[2].xyz, IN.normal);
    norm = normalize(norm);

    pos = mul(projection_matrix, pos);

    OUT.target_pos_x = 0.5 * (shadow_target_width - 1.0) * pos.x;

    OUT.pos_w = pos.w;

    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;

    OUT.position = pos;

    passTextureIndex();

    return OUT;
}
