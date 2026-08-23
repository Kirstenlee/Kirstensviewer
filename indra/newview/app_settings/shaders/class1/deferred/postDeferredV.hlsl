/**
 * @file class1/deferred/postDeferredV.hlsl
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

uniform float2 screen_res;

// S24 (2026-08-19, task #228): this VS is used exclusively by gFXAAProgram
// (confirmed - no other program construction in llviewershadermgr.cpp
// references postDeferredV.glsl/.hlsl), so adding vary_tc/tc_scale here is
// safe and doesn't affect any other shader. postDeferredV.glsl's real GL
// counterpart already outputs both vary_fragcoord (unscaled) AND vary_tc
// (scaled by tc_scale) - fxaaF.glsl's wrapper passes vary_tc, not
// vary_fragcoord, as the real FxaaPixelShader()'s `pos` input. tc_scale
// itself was already being uploaded correctly by LLPipeline::applyFXAA()
// (uniform2f(LLShaderMgr::FXAA_TC_SCALE, ...), real under DX_RENDER since
// task #107) - this HLSL vertex shader just never had anywhere for it to
// land.
uniform float2 tc_scale;

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 vary_fragcoord : TEXCOORD0;
    float2 vary_tc : TEXCOORD1;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;
    float4 pos = float4(IN.position.xyz, 1.0);
    OUT.position = pos;
    OUT.vary_tc = (pos.xy * 0.5 + 0.5) * tc_scale;
    OUT.vary_fragcoord = (pos.xy * 0.5 + 0.5);
    return OUT;
}
