/**
 * @file class1/deferred/RCASF.hlsl
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

/*[EXTRA_CODE_HERE]*/

#ifndef A_CPU

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);

uniform uint4 rcas_param;
uniform float2 out_screen_res;

struct PSInput
{
    // SV_Position must stay declared here - see CASF.hlsl/uiF.hlsl's matching
    // comment (omitting it shifts every VS/PS register for bare-Varying PS
    // inputs).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

// AMD FidelityFX FSR1 RCAS (Robust Contrast Adaptive Sharpening) - direct
// HLSL port of FsrRcasF()/FsrRcasCon() from ffx_fsr1.h.
// See: https://github.com/GPUOpen-Effects/FidelityFX-FSR
//
// Same approach as CASF.hlsl: AMD's ffx_a.h bit-trick reciprocal/clamp
// approximations are not ported - real divides and HLSL's saturate()/clamp()
// are used instead. The optional FSR_RCAS_DENOISE/FSR_RCAS_PASSTHROUGH_ALPHA
// branches are folded in as always-on (denoise) or handled directly (alpha
// passthrough via the already-loaded center sample) rather than preserved as
// unused #ifdef branches - this project's own file carries only the single
// path it actually uses, same as CASF.hlsl's "noScaling==true branch only"
// note.
//
// FsrRcasCon's CPU-side setup (sharpness -> exp2(-sharpness), stored as the
// raw bit pattern of a float) is hand-rolled directly in
// LLPipeline::applyFSR1Rcas() (pipeline.cpp) - FsrRcasCon itself is two real
// lines of math, not worth vendoring a second AMD portability file for.
float4 main(PSInput IN) : SV_Target
{
    // S24: SV_Position is in ABSOLUTE backbuffer pixel coordinates, not
    // texture-relative ones - when this pass is chained after FSR1 EASU
    // (LLPipeline::applyFSR1RcasToBoundTarget(), reading from
    // mFSR1EasuBuffer while writing to the real, chrome-offset-viewport
    // backbuffer), diffuseRect (mFSR1EasuBuffer) has its own (0,0) origin
    // that does NOT match the backbuffer's - using IN.position.xy directly
    // read out of that texture's bounds for every chrome-offset row,
    // Load()'d as black (D3D11's defined out-of-bounds Load() result). Same
    // fix as EASUF.hlsl: derive the texture-relative pixel position from
    // vary_fragcoord (0-1, viewport-relative) and out_screen_res instead -
    // correct for both this chained case AND applyFSR1Rcas()'s own
    // same-resolution case (intermediate target, viewport always matches
    // that target's own (0,0)-origin dimensions there too).
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);
    int2 ip = int2(tc * out_screen_res);

    // Algorithm uses a minimal 3x3 cross neighborhood:
    //     b
    //   d e f
    //     h
    float4 ee = diffuseRect.Load(int3(ip, 0));
    float3 b = diffuseRect.Load(int3(ip + int2( 0, -1), 0)).rgb;
    float3 d = diffuseRect.Load(int3(ip + int2(-1,  0), 0)).rgb;
    float3 e = ee.rgb;
    float3 f = diffuseRect.Load(int3(ip + int2( 1,  0), 0)).rgb;
    float3 h = diffuseRect.Load(int3(ip + int2( 0,  1), 0)).rgb;

    // Luma times 2 (no FsrRcasInputF color-space transform needed - input is
    // already display-encoded by the gamma step, same as CASF.hlsl's note).
    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

    // Noise detection - reduces sharpening on grain/noise, focuses on real
    // edges (FSR_RCAS_DENOISE, always applied here).
    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
    float mx5 = max(bL, max(dL, max(eL, max(fL, hL))));
    float mn5 = min(bL, min(dL, min(eL, min(fL, hL))));
    nz = saturate(abs(nz) / max(mx5 - mn5, 1e-6));
    nz = -0.5 * nz + 1.0;

    // Min/max of the ring (b,d,f,h only, not the center e).
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));

    // Limiters - this is the limit of providing unnatural results for
    // sharpening (FSR_RCAS_LIMIT = 0.25 - 1.0/16.0).
    const float FSR_RCAS_LIMIT = 0.1875;
    const float2 peakC = float2(1.0, -4.0);
    float3 hitMin = min(mn4, e) / (4.0 * mx4);
    float3 hitMax = (peakC.x - max(mx4, e)) / (4.0 * mn4 + peakC.y);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * asfloat(rcas_param.x);
    lobe *= nz;

    // Resolve.
    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    float3 color = (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;

    return float4(color, ee.a);
}
#endif
