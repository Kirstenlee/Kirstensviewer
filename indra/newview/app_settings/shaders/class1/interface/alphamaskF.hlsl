/**
 * @file class1/interface/alphamaskF.hlsl
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

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

uniform float minimum_alpha;

#include "varying/uiVarying.hlsli"

// S24 (2026-08-02): confirmed via fxc.exe disassembly (uiF.hlsl's own fix,
// same shared UIVarying struct) - the VS's SV_Position consumes a real,
// numbered output register, shifting every subsequent interpolant by one.
// A bare UIVarying PS input has no SV_Position field, so its first member
// starts at register 0 instead of 1 - a genuine register mismatch, not
// just a hypothetical one. Wrapping restores identical numbering.
struct PSInput
{
    float4 position : SV_Position;
    UIVarying varying;
};

float4 main(PSInput IN) : SV_Target
{
    float4 col = IN.varying.vertex_color*diffuseMap.Sample(diffuseMapSampler, IN.varying.vary_texcoord0.xy);
    if (col.a < minimum_alpha)
    {
        discard;
    }

    return max(col, float4(0, 0, 0, 0));
}
