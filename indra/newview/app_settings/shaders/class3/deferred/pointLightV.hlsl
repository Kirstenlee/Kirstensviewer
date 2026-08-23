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

// S24 (2026-08-05): was "float2 texcoord0 : TEXCOORD0;" too - never read in
// main() below, and pointLightV.glsl (the original) only ever declares
// "in vec3 position" - no texcoord0 input at all. Under DX_RENDER, an
// input struct field is part of the VS's reflected input SIGNATURE
// regardless of whether the shader body reads it, so this unused field
// forced CreateInputLayout() to require a vertex buffer provide TEXCOORD0
// data - confirmed via a direct D3D11 debug-layer error ("CreateInputLayout
// failed... expects to read an element with SemanticName/Index:
// 'TEXCOORD'/0, but the declaration doesn't provide a matching name")
// against DXPipeline::renderDeferredLighting()'s local-lights cube buffer
// (position-only, matching GL's own equally position-only mCubeVB) -
// meaning every local-light draw silently submitted zero geometry (no
// Input Assembler bound at all) since this pass was first built. Removed
// to match the GLSL original exactly.
struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    PointLightVarying varying;
};

// S24 (2026-08-05): was "uniform float3 trans_center;", read directly into
// OUT.varying.trans_center with no transform at all, and IN.position was
// never scaled/offset by the light's actual size/center either - a real
// porting bug (not a DX_RENDER-specific one - this file has been wrong
// since the original GLSL->HLSL port, just never exercised until DX_RENDER
// built its first local-lights pass). pointLightV.glsl's actual logic:
// `p = position*size+center; pos = modelview_projection_matrix*p;
// trans_center = (modelview_matrix*center).xyz;` - center/size are the
// real per-light uniforms C++ sets (LLShaderMgr::LIGHT_CENTER/LIGHT_SIZE,
// reserved names "center"/"size" - confirmed via llshadermgr.cpp), and
// trans_center is a DERIVED value for the fragment shader, never an input
// uniform at all. Restored to match.
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
