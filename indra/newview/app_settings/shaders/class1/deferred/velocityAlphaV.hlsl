/**
 * @file class1/deferred/velocityAlphaV.hlsl
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

uniform float4x4 modelview_projection_matrix;
uniform float4x4 modelview_matrix;
uniform float4x4 projection_matrix;
uniform float4x4 last_modelview_matrix;
uniform float4x4 last_object_matrix;
uniform float4x4 texture_matrix0;

void passTextureIndex();
void writeVaryVelocity(float4 pos, float4 last_pos, out float4 vary_cur_clip, out float4 vary_last_clip);

#ifdef HAS_SKIN
float4x4 getObjectSkinnedTransform();
float4x4 getLastObjectSkinnedTransform();
#endif

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vary_cur_clip : TEXCOORD0;
    float4 vary_last_clip : TEXCOORD1;
    float2 vary_texcoord0 : TEXCOORD2;
    float4 vertex_color : COLOR0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;
    OUT.vertex_color = IN.diffuse_color;

    passTextureIndex();

#ifdef HAS_SKIN
    float4x4 cur_mat = getObjectSkinnedTransform();
    float4 pos = mul(projection_matrix, mul(modelview_matrix, mul(cur_mat, float4(IN.position.xyz, 1.0))));
    OUT.position = pos;

    float4x4 last_mat = getLastObjectSkinnedTransform();
    float4 last_pos = mul(projection_matrix, mul(last_modelview_matrix, mul(last_mat, float4(IN.position.xyz, 1.0))));
#else
    float4 pos = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
    OUT.position = pos;

    float4 last_pos = mul(projection_matrix, mul(last_modelview_matrix, mul(last_object_matrix, float4(IN.position.xyz, 1.0))));
#endif

    writeVaryVelocity(pos, last_pos, OUT.vary_cur_clip, OUT.vary_last_clip);

    return OUT;
}
