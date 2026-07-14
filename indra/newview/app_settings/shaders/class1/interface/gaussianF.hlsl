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
    float2 vary_texcoord0 : TEXCOORD0;
};

// get linear depth value given a depth buffer sample d and znear and zfar values
float linearDepth(float d, float znear, float zfar);

float4 main(PSInput IN) : SV_Target
{
    float3 col = float3(0, 0, 0);

    float w[9] = { 0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002 };

    for (int i = 0; i < 9; ++i)
    {
        float2 tc = IN.vary_texcoord0 + (i-4)*direction*resScale;
        col += diffuseRect.Sample(diffuseRectSampler, tc).rgb * w[i];
    }

    return max(float4(col, 0.0), float4(0, 0, 0, 0));
}
