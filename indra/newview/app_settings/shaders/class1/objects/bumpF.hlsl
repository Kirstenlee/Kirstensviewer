/**
 * @file class1/objects/bumpF.hlsl
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

Texture2D texture0 : register(t0);
Texture2D texture1 : register(t1);
SamplerState texture0Sampler : register(s0);
SamplerState texture1Sampler : register(s1);

void mirrorClip(float3 pos);

#include "varying/bumpVarying.hlsli"

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input.
struct PSInput
{
    float4 position : SV_Position;
    BumpVarying varying;
};

float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.varying.vary_position);
    float tex0 = texture0.Sample(texture0Sampler, IN.varying.vary_texcoord0.xy).a;
    float tex1 = texture1.Sample(texture1Sampler, IN.varying.vary_texcoord1.xy).a;

    return max(float4(tex0 + (1.0 - tex1) - 0.5, tex0 + (1.0 - tex1) - 0.5, tex0 + (1.0 - tex1) - 0.5, tex0 + (1.0 - tex1) - 0.5), float4(0, 0, 0, 0));
}
