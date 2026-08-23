/**
 * @file varying/pbrMetallicRoughnessVarying.hlsli
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

// Shared vertex-to-pixel varying struct for pbrmetallicroughnessV.hlsl/
// pbrmetallicroughnessF.hlsl - see varying/uiVarying.hlsli's comment for why
// this exists. Declares every field unconditionally (matching VSOutput's own
// unconditional declaration) rather than mirroring PSInput's original
// "#ifndef UNLIT" guard around the normal/tangent/UV fields - under UNLIT,
// the pixel shader simply never reads the extra nested fields, which is
// harmless and valid HLSL (same as any PS ignoring some VS outputs), and
// keeps this one struct correct for both UNLIT and lit compiles without a
// second, conditional variant.
struct PBRMetallicRoughnessVarying
{
    float3 vary_position : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float2 base_color_uv : TEXCOORD1;
    float2 emissive_uv : TEXCOORD2;
    float3 vary_normal : TEXCOORD3;
    float3 vary_tangent : TEXCOORD4;
    nointerpolation float vary_sign : TEXCOORD5;
    float2 normal_uv : TEXCOORD6;
    float2 metallic_roughness_uv : TEXCOORD7;
    float2 occlusion_uv : TEXCOORD8;
    // S24 (2026-08-19, task #238): needed by the ALPHA_BLEND lit branch's
    // shadow-lookup tc (matches pbrmetallicroughnessV.glsl's own
    // #ifdef ALPHA_BLEND vary_fragcoord). Declared unconditionally, same
    // rationale as every other field in this struct (see top comment) -
    // harmless/unused under the G-buffer and UNLIT permutations.
    float3 vary_fragcoord : TEXCOORD9;
};
