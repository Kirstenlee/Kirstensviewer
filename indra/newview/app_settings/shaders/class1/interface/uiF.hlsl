/**
 * @file class1/interface/uiF.hlsl
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

#include "varying/uiVarying.hlsli"

// S24 (2026-08-02): confirmed via fxc.exe disassembly, not inference - the
// VS's OWN output signature (uiV.hlsl) assigns SV_Position to a REAL,
// NUMBERED output register (0), pushing TEXCOORD0 to register 1 and
// COLOR0 to register 2. This PSInput previously had no SV_Position field
// at all, so its first declared member (TEXCOORD0) started fresh at
// register 0 - a genuine register mismatch (D3D11 debug-layer id=343,
// "TEXCOORD... mismatched hardware registers"), invisible from reading
// either file's source alone since both LOOKED structurally identical.
// Wrapping UIVarying in a local struct with its own (unused) SV_Position
// field first mirrors the VS's exact shape, restoring identical register
// numbering on both sides.
struct PSInput
{
    float4 position : SV_Position;
    UIVarying varying;
};

float4 main(PSInput IN) : SV_Target
{
    return IN.varying.vertex_color*diffuseMap.Sample(diffuseMapSampler, IN.varying.vary_texcoord0.xy);
}
