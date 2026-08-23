/**
 * @file varying/deferredBumpVarying.hlsli
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

// Shared vertex-to-pixel varying struct for class1/deferred/bumpV.hlsl/
// bumpF.hlsl - see varying/uiVarying.hlsli's comment for why this exists.
// (Not to be confused with class1/objects/bumpV.hlsl/F.hlsl - a different,
// smaller shader pair with its own header, BumpVarying.)
struct DeferredBumpVarying
{
    float3 vary_mat0 : TEXCOORD0;
    float3 vary_mat1 : TEXCOORD1;
    float3 vary_mat2 : TEXCOORD2;
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD3;
    float3 vary_position : TEXCOORD4;
};
