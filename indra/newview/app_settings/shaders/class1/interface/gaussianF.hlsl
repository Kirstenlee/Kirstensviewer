/**
 * @file class1/interface/gaussianF.hlsl
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

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);

uniform float resScale;

// texture direction, will be <1, 0> or <0, 1>
uniform float2 direction;

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_texcoord0 : TEXCOORD0;
};

// get linear depth value given a depth buffer sample d and znear and zfar values
float linearDepth(float d, float znear, float zfar);

float4 main(PSInput IN) : SV_Target
{
    float3 col = float3(0, 0, 0);

    float w[9] = { 0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002 };

    // S24 (2026-08-11, quick-win origin sweep): GL-vs-D3D11 texture-origin
    // flip - same bug class as task #158/#185. Flipped at the Sample() call
    // (not the base vary_texcoord0) so the per-tap offset's Y component
    // (relevant only for the vertical-direction pass) still applies
    // correctly - safe regardless, since the 9-tap kernel weights above are
    // symmetric (w[i]==w[8-i]), same reasoning already verified for
    // glowF.hlsl's identical flipV() fix.
    for (int i = 0; i < 9; ++i)
    {
        float2 tc = IN.vary_texcoord0 + (i-4)*direction*resScale;
        col += diffuseRect.Sample(diffuseRectSampler, float2(tc.x, 1.0 - tc.y)).rgb * w[i];
    }

    return max(float4(col, 0.0), float4(0, 0, 0, 0));
}
