/**
 * @file class3/deferred/pointLightV.hlsl
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

uniform float4x4 modelview_projection_matrix;
uniform float4x4 modelview_matrix;

#include "varying/pointLightVarying.hlsli"

// S24: keep this position-only, matching GL's mCubeVB - an unused field here (even one
// the shader body never reads) is still part of the VS's reflected input SIGNATURE under
// DX_RENDER, so CreateInputLayout() would require a vertex buffer supply it or fail.
struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    PointLightVarying varying;
};

// S24: center/size are the real per-light uniforms (LLShaderMgr::LIGHT_CENTER/LIGHT_SIZE,
// reserved names "center"/"size" - llshadermgr.cpp). trans_center is derived here for the
// fragment shader, never an input uniform itself - see main()'s transform below.
uniform float3 center;
uniform float  size;

VSOutput main(VSInput IN)
{
    VSOutput OUT;
    float3 p = IN.position.xyz * size + center;
    float4 pos = mul(modelview_projection_matrix, float4(p, 1.0));
    OUT.position = pos;
    OUT.varying.vary_fragcoord = pos;
    OUT.varying.trans_center = mul(modelview_matrix, float4(center, 1.0)).xyz;
    return OUT;
}
