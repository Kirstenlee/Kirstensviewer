/**
 * @file class1/deferred/fxaaF.hlsl
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

#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 12

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);

uniform float2 screen_res;
uniform float2 fxaaQualityRcpFrame;
uniform float2 fxaaQualitySubpix;
uniform float fxaaQualityEdgeThreshold;
uniform float fxaaQualityEdgeThresholdMin;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    float2 pos = IN.vary_fragcoord;
    float4 color;

    // FXAA 3.11 implementation for HLSL 5
    float3 rgbNW = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(-1.0, -1.0) * fxaaQualityRcpFrame, 0).rgb;
    float3 rgbNE = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(1.0, -1.0) * fxaaQualityRcpFrame, 0).rgb;
    float3 rgbSW = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(-1.0, 1.0) * fxaaQualityRcpFrame, 0).rgb;
    float3 rgbSE = diffuseRect.SampleLevel(diffuseRectSampler, pos + float2(1.0, 1.0) * fxaaQualityRcpFrame, 0).rgb;
    float3 rgbM = diffuseRect.SampleLevel(diffuseRectSampler, pos, 0).rgb;

    float3 luma = float3(0.299, 0.587, 0.144);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float2 dir = float2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), ((lumaNW + lumaSW) - (lumaNE + lumaSE)));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * 0.0833), 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(float2(fxaaQualitySubpix.x, fxaaQualitySubpix.x), max(float2(-fxaaQualitySubpix.x, -fxaaQualitySubpix.x), dir * rcpDirMin)) * fxaaQualityRcpFrame;

    float3 rgbA = 0.5 * (
        diffuseRect.SampleLevel(diffuseRectSampler, pos + dir * (1.0 / 3.0 - 0.5), 0).rgb +
        diffuseRect.SampleLevel(diffuseRectSampler, pos + dir * (2.0 / 3.0 - 0.5), 0).rgb);

    float3 rgbB = rgbA * 0.5 + 0.25 * (
        diffuseRect.SampleLevel(diffuseRectSampler, pos + dir * (0.0 / 3.0 - 0.5), 0).rgb +
        diffuseRect.SampleLevel(diffuseRectSampler, pos + dir * (3.0 / 3.0 - 0.5), 0).rgb);

    float lumaB = dot(rgbB, luma);
    if ((lumaB < lumaMin) || (lumaB > lumaMax))
    {
        color = float4(rgbA, 1.0);
    }
    else
    {
        color = float4(rgbB, 1.0);
    }

    return color;
}
