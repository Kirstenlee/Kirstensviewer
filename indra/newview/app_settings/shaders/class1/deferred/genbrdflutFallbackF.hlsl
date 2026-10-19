/**
 * @file class1/deferred/genbrdflutFallbackF.hlsl
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

// S24: emergency fallback only - fills mPbrBrdfLut when the baked LUT asset
// (see LLPipeline::createLUTBuffers()) is missing or fails to load. Uses
// Karis' closed-form analytic approximation of the same split-sum
// environment BRDF integral genbrdflutF.hlsl computes via Monte Carlo
// ("Physically Based Shading on Mobile", Brian Karis, Epic, 2014) - no
// loop, nothing for a GPU driver's compiler to pathologically unroll, so
// this is safe to compile and run on any hardware. Close to, but not
// bit-identical with, the real LUT - acceptable since this path should
// basically never be hit in a normal install.
/*[EXTRA_CODE_HERE]*/

#include "varying/genBrdfLutVarying.hlsli"

float2 EnvBRDFApprox(float roughness, float NoV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

// Same SV_Position/varying register-ordering note as genbrdflutF.hlsl's PSInput.
struct PSInput
{
    float4 position : SV_Position;
    GenBrdfLutVarying varying;
};

float4 main(PSInput IN) : SV_Target
{
    float NoV = IN.varying.vary_uv.x;
    float roughness = 1.0 - IN.varying.vary_uv.y;
    return float4(EnvBRDFApprox(roughness, NoV), 0.0, 1.0);
}
