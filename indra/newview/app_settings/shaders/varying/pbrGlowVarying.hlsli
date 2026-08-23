/**
 * @file varying/pbrGlowVarying.hlsli
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

// Shared vertex-to-pixel varying struct for pbrglowV.hlsl/pbrglowF.hlsl - see
// varying/uiVarying.hlsli's comment for why this exists (one canonical field
// list instead of two independently hand-typed copies that can drift). This
// specific pair previously had a real bug of the same root cause: pbrglowF.hlsl
// once carried a dead, never-read "vary_position:TEXCOORD0" field (inherited
// harmlessly from the original GLSL, where an unread `in` is inert) that
// consumed a real HLSL register slot and shifted every field after it - see
// project_dxrender_vsps_linkage_bug memory.
struct PBRGlowVarying
{
    float2 base_color_texcoord : TEXCOORD0;
    float2 emissive_texcoord : TEXCOORD1;
    float4 vertex_emissive : COLOR0;
};
