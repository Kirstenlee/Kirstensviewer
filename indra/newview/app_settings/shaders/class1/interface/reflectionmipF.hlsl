/**
 * @file class1/interface/reflectionmipF.hlsl
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

struct PSInput
{
    // SV_Position required here - its absence shifts every interpolant register (see uiF.hlsl).
    float4 position : SV_Position;

    float2 vary_texcoord0 : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    // GL-vs-D3D11 texture-origin flip, same convention as the rest of this
    // post-fx/reflection-probe chain (vary_texcoord0 = position.xy*0.5+0.5,
    // from splattexturerectV.hlsl).
    float3 col = diffuseRect.Sample(diffuseRectSampler, float2(IN.vary_texcoord0.x, 1.0 - IN.vary_texcoord0.y)).rgb;
    return float4(col, 0.0);
}
