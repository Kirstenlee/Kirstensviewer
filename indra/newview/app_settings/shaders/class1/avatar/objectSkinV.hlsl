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

// S24 (2026-08-09, task #157): row_major is required here, not stylistic -
// HLSL's DEFAULT (column_major) packing would store each float3x4 element
// as 4 float4 registers (one per column, 3 used + 1 padding each = 16
// floats/matrix), but the C++ side (LLRenderPass::uploadMatrixPalette(),
// lldrawpool.cpp) uploads a COMPACT 12-float-per-matrix affine encoding
// (3 rows x 4 floats: rotation+scale in columns 0-2, translation in
// column 3 - matching this file's own matrixPalette[i][row].w translation
// extraction below). row_major makes each element exactly 3 full
// registers (12 floats, zero padding), matching that layout byte-for-byte
// so DXShader::uniformMatrix3x4fv()'s DX_RENDER branch (llglslshader.cpp)
// can do a straight contiguous copy, the same "no repacking needed"
// approach already established for setUniformMatrix4().
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

    // S24 (2026-08-09, task #157): the original GLSL sets ret[0..3] as
    // COLUMNS (GLSL mat4[i] indexes columns), so ret[3]=vec4(trans,1.0)
    // is genuinely the translation column there. HLSL's M[i] ALWAYS means
    // ROW i regardless of row_major/column_major storage - a mechanical
    // line-by-line port put translation in row 3 instead, which the
    // caller's mul(trans_matrix, vertex) (row-based: result[i] =
    // dot(row_i, vertex)) never adds to x/y/z at all, and instead
    // corrupts pos.w with dot(trans, vertex.xyz)+vertex.w. Every rigged
    // mesh-attachment vertex (mesh bodies/heads/hair/clothing - anything
    // using this function, i.e. every POOL using PASS_*_RIGGED) lost its
    // translation entirely. Fixed by building the row-based affine matrix
    // directly (translation as each row's 4th element, row 3 = identity),
    // matching avatarSkinV.hlsl's getSkinnedTransform() - already correct
    // there since its matrixPalette rows come pre-packed with translation
    // in their own .w component from the C++ side.
    //
    // S24 (2026-08-09, task #157, round 2): a SECOND, compounding instance
    // of the same GLSL-column-vs-HLSL-row indexing trap, in the rotation
    // extraction above. GLSL's mat3(matrixPalette[i1]) truncates by
    // COLUMN, so its resulting mat3's column j ends up holding source
    // JOINT MATRIX ROW j (traced by hand against the C++ upload packing
    // in LLVOAvatar::updateSkinInfoMatrixPalette) - i.e. GLSL's mat is the
    // TRANSPOSE of the raw joint rotation, and alphaV.glsl's plain
    // `trans * vec4(pos,1.0)` (no extra transpose) confirms that's what
    // the consuming math actually wants (LLMatrix4a's row-vector-on-left
    // convention needs the transpose to work under column-vector mul).
    // HLSL's (float3x3) truncation instead keeps ROW j = source row j
    // verbatim (no transpose) - so `mat` here is the raw joint rotation,
    // not its transpose. transpose(mat) reproduces GLSL's actual value.
    float3x3 matT = transpose(mat);

    float4x4 ret;
    ret[0] = float4(matT[0], trans.x);
    ret[1] = float4(matT[1], trans.y);
    ret[2] = float4(matT[2], trans.z);
    ret[3] = float4(0.0, 0.0, 0.0, 1.0);

    return ret;
}
