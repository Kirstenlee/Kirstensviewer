/**
 * @file class1/avatar/objectSkinV.hlsl
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

uniform float3x4 matrixPalette[MAX_JOINTS_PER_MESH_OBJECT];

float4x4 getObjectSkinnedTransform()
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

    float3x3 mat = (float3x3)matrixPalette[i1] * w.x;
    mat += (float3x3)matrixPalette[i2] * w.y;
    mat += (float3x3)matrixPalette[i3] * w.z;
    mat += (float3x3)matrixPalette[i4] * w.w;

    float3 trans = float3(matrixPalette[i1][0].w, matrixPalette[i1][1].w, matrixPalette[i1][2].w) * w.x;
    trans += float3(matrixPalette[i2][0].w, matrixPalette[i2][1].w, matrixPalette[i2][2].w) * w.y;
    trans += float3(matrixPalette[i3][0].w, matrixPalette[i3][1].w, matrixPalette[i3][2].w) * w.z;
    trans += float3(matrixPalette[i4][0].w, matrixPalette[i4][1].w, matrixPalette[i4][2].w) * w.w;

    float4x4 ret;
    ret[0] = float4(mat[0], 0);
    ret[1] = float4(mat[1], 0);
    ret[2] = float4(mat[2], 0);
    ret[3] = float4(trans, 1.0);

    return ret;
}
