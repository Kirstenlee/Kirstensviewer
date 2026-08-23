/**
 * @file class1/effects/glowExtractV.hlsl
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

// S24 (2026-08-23, task #160 ROOT CAUSE): this shader's VSInput used to
// declare a separate `texcoord0 : TEXCOORD0` vertex attribute and multiply
// position by a modelview_projection_matrix uniform - neither exists in
// the real GLSL source (glowExtractV.glsl: `in vec3 position` only, no
// matrix multiply, `gl_Position = vec4(position, 1.0)`, texcoord derived
// as `position.xy * 0.5 + 0.5`). LLPipeline::mScreenTriangleVB (the vertex
// buffer this shader actually draws with, generateGlow()'s extract pass)
// only ever supplies POSITION data - a live D3D11 debug-layer log
// (DXVertexLayout::getOrCreate) confirmed CreateInputLayout() failing
// outright with E_INVALIDARG for this exact shader ("Glow Extract Shader
// (Post) (+Noise)", data_mask=0x1 i.e. position-only) because the compiled
// vertex shader's input signature demanded a TEXCOORD0 attribute that was
// never actually bound. With no valid input layout, the extract draw call
// never ran, mGlow[2] (and everything downstream: the blur ping-pong,
// mGlow[1], combineGlow()'s composite) stayed permanently empty regardless
// of scene content - the real root cause of glow/bloom producing zero
// bloom under DX_RENDER (task #160). Fixed to match this codebase's own
// already-proven-working full-screen-triangle convention (see
// postDeferredV.hlsl, gFXAAProgram's vertex shader): position-only input,
// no matrix multiply, texcoord derived procedurally.
struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 vary_texcoord0 : TEXCOORD0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;
    float4 pos = float4(IN.position.xyz, 1.0);
    OUT.position = pos;
    OUT.vary_texcoord0 = pos.xy * 0.5 + 0.5;
    return OUT;
}
