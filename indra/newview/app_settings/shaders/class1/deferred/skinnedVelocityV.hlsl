/**
 * @file class1/deferred/skinnedVelocityV.hlsl
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
uniform float4x4 projection_matrix;
uniform float4x4 last_modelview_matrix;

uniform float3x4 lastMatrixPalette[MAX_JOINTS_PER_MESH_OBJECT];

void writeVaryVelocity(float4 pos, float4 last_pos, out float4 vary_cur_clip, out float4 vary_last_clip);

float4x4 getLastObjectSkinnedTransform(float4 weight4)
{
    float4 w = frac(weight4);
    float4 index = floor(weight4);

    index = min(index, float4(MAX_JOINTS_PER_MESH_OBJECT - 1));
    index = max(index, float4(0.0));

    w *= 1.0 / (w.x + w.y + w.z + w.w);

    int i1 = int(index.x);
    int i2 = int(index.y);
    int i3 = int(index.z);
    int i4 = int(index.w);

    float3x3 mat = (float3x3)lastMatrixPalette[i1] * w.x;
    mat += (float3x3)lastMatrixPalette[i2] * w.y;
    mat += (float3x3)lastMatrixPalette[i3] * w.z;
    mat += (float3x3)lastMatrixPalette[i4] * w.w;

    float3 trans = float3(lastMatrixPalette[i1][0].w, lastMatrixPalette[i1][1].w, lastMatrixPalette[i1][2].w) * w.x;
    trans += float3(lastMatrixPalette[i2][0].w, lastMatrixPalette[i2][1].w, lastMatrixPalette[i2][2].w) * w.y;
    trans += float3(lastMatrixPalette[i3][0].w, lastMatrixPalette[i3][1].w, lastMatrixPalette[i3][2].w) * w.z;
    trans += float3(lastMatrixPalette[i4][0].w, lastMatrixPalette[i4][1].w, lastMatrixPalette[i4][2].w) * w.w;

    float4x4 ret;
    ret[0] = float4(mat[0], 0);
    ret[1] = float4(mat[1], 0);
    ret[2] = float4(mat[2], 0);
    ret[3] = float4(trans, 1.0);

    return ret;
}

struct VSInput
{
    float3 position : POSITION;
    float4 weight4 : BLENDWEIGHT;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vary_cur_clip : TEXCOORD0;
    float4 vary_last_clip : TEXCOORD1;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    float4 pos = float4(IN.position.xyz, 1.0);

    float4 current_clip = mul(modelview_projection_matrix, pos);

    float4x4 last_mat = getLastObjectSkinnedTransform(IN.weight4);
    float4 last_clip = mul(projection_matrix, mul(last_modelview_matrix, mul(last_mat, pos)));

    OUT.position = current_clip;

    writeVaryVelocity(current_clip, last_clip, OUT.vary_cur_clip, OUT.vary_last_clip);

    return OUT;
}
