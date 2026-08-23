/**
 * @file varying/uiVarying.hlsli
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

// Shared vertex-to-pixel varying struct for the plain textured-UI shader
// family (uiV/F.hlsl, alphamaskV/F.hlsl - both textured quad + per-vertex
// color, no lighting/skinning). Included (via DXShader::resolveIncludes(),
// not a real HLSL #include - see its comment) by both the vertex and pixel
// stage of each pair, so there is exactly one place that defines this
// field list and its order - the vertex shader's VSOutput and the pixel
// shader's PSInput can no longer independently drift out of sync the way
// uiV.hlsl/uiF.hlsl and alphamaskV.hlsl/alphamaskF.hlsl once did (see
// project_dxrender_vsps_linkage_bug memory). HLSL recursively flattens a
// nested struct field's own members in their declared order, so embedding
// UIVarying inside VSOutput (see uiV.hlsl) produces the exact same
// register layout as using UIVarying directly as PSInput (see uiF.hlsl).
struct UIVarying
{
    float2 vary_texcoord0 : TEXCOORD0;
    float4 vertex_color : COLOR0;
};
