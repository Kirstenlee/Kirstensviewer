/**
 * @file class1/deferred/stereoAnaglyphF.hlsl
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

// Both eyes render a complete, independent frame into their own offscreen
// target (LLPipeline::mStereoEyeL/R); this pass is the only place the two
// eyes' color actually mixes, as shader math rather than persistent
// glColorMask GPU state (the old design's problem: too many unrelated
// setColorWriteMask call sites tree-wide could reset it mid-frame).
//
// Each eye contributes through its own 4x4 color matrix (only the top-left
// 3x3 is meaningful) rather than a hardcoded channel swizzle, so a future
// variant (half-color, Dubois, amber/blue) is just different matrix
// uniforms. Default matrices (DXPipeline::presentStereoComposite())
// reproduce true-color red/cyan: left_matrix = diag(1,0,0,0), right_matrix
// = diag(0,1,1,0).
//
// float4x4 (not float3x3) because LLHLSLShader's name-based uniform-upload
// path under DX_RENDER only supports 4x4 matrices; the 3x3 overload relies
// on GL-only getUniformLocation() dead code. Same reason the two eye
// textures are bound via LLShaderMgr's DIFFUSE_MAP/ALTERNATE_DIFFUSE_MAP
// reserved-uniform-name slots rather than a custom name.
Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

Texture2D altDiffuseMap : register(t1);
SamplerState altDiffuseMapSampler : register(s1);

uniform float4x4 left_eye_matrix;
uniform float4x4 right_eye_matrix;

struct PSInput
{
    float4 position : SV_Position;
    float2 vary_fragcoord : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    // GL-vs-D3D11 texture-origin flip - same convention as every other
    // full-screen post pass reading a render-target SRV in this renderer
    // (see postDeferredNoDoFF.hlsl's identical fix).
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);

    float4 left = float4(diffuseMap.Sample(diffuseMapSampler, tc).rgb, 0.0);
    float4 right = float4(altDiffuseMap.Sample(altDiffuseMapSampler, tc).rgb, 0.0);

    float3 composite = mul(left_eye_matrix, left).rgb + mul(right_eye_matrix, right).rgb;

    return float4(saturate(composite), 1.0);
}
