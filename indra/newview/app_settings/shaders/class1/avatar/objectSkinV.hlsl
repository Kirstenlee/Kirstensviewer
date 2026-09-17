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

// row_major is required, not stylistic: the C++ side
// (LLRenderPass::uploadMatrixPalette(), lldrawpool.cpp) uploads a compact
// 12-float-per-matrix affine encoding (3 rows x 4 floats: rotation+scale in
// columns 0-2, translation in column 3). column_major would pad each
// column to a full float4 (16 floats/matrix), breaking that layout.
uniform row_major float3x4 matrixPalette[MAX_JOINTS_PER_MESH_OBJECT];

float4x4 getObjectSkinnedTransform()
{
    float4 w = frac(weight4);
    float4 index = floor(weight4);

    float max_joint_index = MAX_JOINTS_PER_MESH_OBJECT - 1;
    index = min(index, float4(max_joint_index, max_joint_index, max_joint_index, max_joint_index));
    index = max(index, float4(0.0, 0.0, 0.0, 0.0));

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

    // HLSL's M[i] always indexes ROWS regardless of row_major/column_major
    // storage, unlike GLSL's mat4[i] (columns). Translation must therefore
    // be built as each row's 4th element (row 3 = identity), matching
    // avatarSkinV.hlsl's getSkinnedTransform().
    //
    // Similarly, HLSL's (float3x3) truncation of matrixPalette[i1] keeps
    // rows verbatim (no transpose), whereas GLSL's mat3() truncation-by-
    // column effectively transposes the joint rotation. transpose(mat)
    // below reproduces the value the row-vector-on-left convention expects.
    float3x3 matT = transpose(mat);

    float4x4 ret;
    ret[0] = float4(matT[0], trans.x);
    ret[1] = float4(matT[1], trans.y);
    ret[2] = float4(matT[2], trans.z);
    ret[3] = float4(0.0, 0.0, 0.0, 1.0);

    return ret;
}
