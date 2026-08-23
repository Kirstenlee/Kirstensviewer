/**
 * @file class1/interface/glowcombineF.hlsl
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

/*[EXTRA_CODE_HERE]*/

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);
Texture2D emissiveRect : register(t1);
SamplerState emissiveRectSampler : register(s1);

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 tc : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    // S24 (2026-08-09, task #138): same GL-vs-D3D11 texture-origin mismatch
    // already fixed for postDeferredGammaCorrect.hlsl/glowExtractF.hlsl/
    // glowF.hlsl - flip belongs here, at the actual Texture2D .Sample() call
    // site, not in glowcombineV.hlsl (tc = position.xy*0.5+0.5, same
    // pattern as postDeferredNoTCV.hlsl - kept in GL's original convention).
    float2 tc = float2(IN.tc.x, 1.0 - IN.tc.y);
    return diffuseRect.Sample(diffuseRectSampler, tc) + emissiveRect.Sample(emissiveRectSampler, tc);
}
