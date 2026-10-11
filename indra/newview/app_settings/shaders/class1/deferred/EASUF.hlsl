/**
 * @file class1/deferred/EASUF.hlsl
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

// FsrEasuCon()'s con0-con3 (newview/pipeline.cpp's applyFSR1Easu() computes
// these CPU-side, same hand-rolled approach RCASF.hlsl's rcas_param uses -
// not worth vendoring a second AMD portability file for).
uniform uint4 easu_const0;
uniform uint4 easu_const1;
uniform uint4 easu_const2;
uniform uint4 easu_const3;
uniform float2 out_screen_res;

struct PSInput
{
    // SV_Position must stay declared here - see CASF.hlsl/RCASF.hlsl's
    // matching comment.
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

// AMD FidelityFX FSR1 EASU (Edge Adaptive Spatial Upsampling) - direct HLSL
// port of FsrEasuTapF()/FsrEasuSetF()/FsrEasuF()/FsrEasuCon() from
// ffx_fsr1.h, following the exact AMD-prescribed HLSL Gather wiring from
// AMD's own sample/src/DX12/FSR_Pass.hlsl (SAMPLE_SLOW_FALLBACK path) -
// plain GatherRed/GatherGreen/GatherBlue with a zero offset; no manual tap
// reordering, the native HLSL Gather component order already matches what
// FsrEasuCon()'s offset constants and FsrEasuF()'s tap indexing expect.
// See: https://github.com/GPUOpen-Effects/FidelityFX-FSR
//
// AMD's own variable names (b/c/e/f/g/h/i/j/k/l/n/o tap grid, bczzR/ijfeR/
// klhgR/zzonR gather groups) are kept as-is rather than renamed, since this
// is an intricate 12-tap directional resampler - a literal, traceable
// translation is safer than a "cleaned up" rewrite here. Same approach as
// CASF.hlsl/RCASF.hlsl: AMD's ffx_a.h bit-trick reciprocal/rsqrt
// approximations (APrxLoRcpF1/APrxLoRsqF1) are not ported - real divides
// and HLSL's native rsqrt() are used instead.

float4 FsrEasuRF(float2 p) { return diffuseRect.GatherRed(diffuseRectSampler, p, int2(0, 0)); }
float4 FsrEasuGF(float2 p) { return diffuseRect.GatherGreen(diffuseRectSampler, p, int2(0, 0)); }
float4 FsrEasuBF(float2 p) { return diffuseRect.GatherBlue(diffuseRectSampler, p, int2(0, 0)); }

// Filtering for a given tap for the scalar.
void FsrEasuTapF(
    inout float3 aC, // Accumulated color, with negative lobe.
    inout float aW, // Accumulated weight.
    float2 off, // Pixel offset from resolve position to tap.
    float2 dir, // Gradient direction.
    float2 len, // Length.
    float lob, // Negative lobe strength.
    float clp, // Clipping point.
    float3 c) // Tap color.
{
    float2 v;
    v.x = (off.x * ( dir.x)) + (off.y * dir.y);
    v.y = (off.x * (-dir.y)) + (off.y * dir.x);
    v *= len;
    float d2 = v.x * v.x + v.y * v.y;
    d2 = min(d2, clp);
    float wB = (2.0 / 5.0) * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

// Accumulate direction and length.
void FsrEasuSetF(
    inout float2 dir,
    inout float len,
    float2 pp,
    bool biS, bool biT, bool biU, bool biV,
    float lA, float lB, float lC, float lD, float lE)
{
    float w = 0.0;
    if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
    if (biT) w =        pp.x  * (1.0 - pp.y);
    if (biU) w = (1.0 - pp.x) *        pp.y;
    if (biV) w =        pp.x  *        pp.y;

    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    // S24: AMD's APrxLoRcpF1 (not ported, see this file's header comment)
    // degrades gracefully near zero; a literal divide does not - a flat/
    // uniform area (lD==lC==lB, common for sky/ground) makes lenX exactly 0,
    // giving Inf here and then 0*Inf==NaN at "abs(dirX)*lenX" below (dirX is
    // also 0 in that same flat case). Clamp the denominator instead.
    lenX = 1.0 / max(lenX, 1e-8);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = saturate(abs(dirX) * lenX);
    lenX *= lenX;
    len += lenX * w;

    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = 1.0 / max(lenY, 1e-8); // same zero-denominator guard as lenX above
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = saturate(abs(dirY) * lenY);
    lenY *= lenY;
    len += lenY * w;
}

float3 FsrEasuF(float2 ip)
{
    // Get position of 'f'.
    float2 pp = ip * asfloat(easu_const0.xy) + asfloat(easu_const0.zw);
    float2 fp = floor(pp);
    pp -= fp;

    // 12-tap kernel.
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    float2 p0 = fp * asfloat(easu_const1.xy) + asfloat(easu_const1.zw);
    float2 p1 = p0 + asfloat(easu_const2.xy);
    float2 p2 = p0 + asfloat(easu_const2.zw);
    float2 p3 = p0 + asfloat(easu_const3.xy);
    float4 bczzR = FsrEasuRF(p0);
    float4 bczzG = FsrEasuGF(p0);
    float4 bczzB = FsrEasuBF(p0);
    float4 ijfeR = FsrEasuRF(p1);
    float4 ijfeG = FsrEasuGF(p1);
    float4 ijfeB = FsrEasuBF(p1);
    float4 klhgR = FsrEasuRF(p2);
    float4 klhgG = FsrEasuGF(p2);
    float4 klhgB = FsrEasuBF(p2);
    float4 zzonR = FsrEasuRF(p3);
    float4 zzonG = FsrEasuGF(p3);
    float4 zzonB = FsrEasuBF(p3);

    // Simplest multi-channel approximate luma possible (luma times 2).
    float4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
    float4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
    float4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
    float4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);
    float bL = bczzL.x;
    float cL = bczzL.y;
    float iL = ijfeL.x;
    float jL = ijfeL.y;
    float fL = ijfeL.z;
    float eL = ijfeL.w;
    float kL = klhgL.x;
    float lL = klhgL.y;
    float hL = klhgL.z;
    float gL = klhgL.w;
    float oL = zzonL.z;
    float nL = zzonL.w;

    // Accumulate for bilinear interpolation.
    float2 dir = float2(0.0, 0.0);
    float len = 0.0;
    FsrEasuSetF(dir, len, pp, true,  false, false, false, bL, eL, fL, gL, jL);
    FsrEasuSetF(dir, len, pp, false, true,  false, false, cL, fL, gL, hL, kL);
    FsrEasuSetF(dir, len, pp, false, false, true,  false, fL, iL, jL, kL, nL);
    FsrEasuSetF(dir, len, pp, false, false, false, true,  gL, jL, kL, lL, oL);

    // Normalize with approximation, and cleanup close to zero.
    float2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < (1.0 / 32768.0);
    dirR = rsqrt(max(dirR, 1e-12));
    dirR = zro ? 1.0 : dirR;
    dir.x = zro ? 1.0 : dir.x;
    dir *= dirR;

    // Transform from {0 to 2} to {0 to 1} range, and shape with square.
    len = len * 0.5;
    len *= len;

    // Stretch kernel {1.0 vert|horz, to sqrt(2.0) on diagonal}.
    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
    float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);

    // Based on the amount of 'edge', the window shifts from +/-{sqrt(2.0) to slightly beyond 2.0}.
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;

    // Accumulation mixed with min/max of 4 nearest.
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    float3 ijf = float3(ijfeR.z, ijfeG.z, ijfeB.z);
    float3 klh = float3(klhgR.w, klhgG.w, klhgB.w);
    float3 ijg = float3(ijfeR.y, ijfeG.y, ijfeB.y);
    float3 klk = float3(klhgR.x, klhgG.x, klhgB.x);
    float3 min4 = min(min(min(ijf, klh), ijg), klk);
    float3 max4 = max(max(max(ijf, klh), ijg), klk);

    float3 aC = float3(0.0, 0.0, 0.0);
    float aW = 0.0;
    FsrEasuTapF(aC, aW, float2( 0.0, -1.0) - pp, dir, len2, lob, clp, float3(bczzR.x, bczzG.x, bczzB.x)); // b
    FsrEasuTapF(aC, aW, float2( 1.0, -1.0) - pp, dir, len2, lob, clp, float3(bczzR.y, bczzG.y, bczzB.y)); // c
    FsrEasuTapF(aC, aW, float2(-1.0,  1.0) - pp, dir, len2, lob, clp, float3(ijfeR.x, ijfeG.x, ijfeB.x)); // i
    FsrEasuTapF(aC, aW, float2( 0.0,  1.0) - pp, dir, len2, lob, clp, ijg);                                // j
    FsrEasuTapF(aC, aW, float2( 0.0,  0.0) - pp, dir, len2, lob, clp, ijf);                                // f
    FsrEasuTapF(aC, aW, float2(-1.0,  0.0) - pp, dir, len2, lob, clp, float3(ijfeR.w, ijfeG.w, ijfeB.w)); // e
    FsrEasuTapF(aC, aW, float2( 1.0,  1.0) - pp, dir, len2, lob, clp, klk);                                // k
    FsrEasuTapF(aC, aW, float2( 2.0,  1.0) - pp, dir, len2, lob, clp, float3(klhgR.y, klhgG.y, klhgB.y)); // l
    FsrEasuTapF(aC, aW, float2( 2.0,  0.0) - pp, dir, len2, lob, clp, float3(klhgR.z, klhgG.z, klhgB.z)); // h
    FsrEasuTapF(aC, aW, float2( 1.0,  0.0) - pp, dir, len2, lob, clp, klh);                                // g
    FsrEasuTapF(aC, aW, float2( 1.0,  2.0) - pp, dir, len2, lob, clp, float3(zzonR.z, zzonG.z, zzonB.z)); // o
    FsrEasuTapF(aC, aW, float2( 0.0,  2.0) - pp, dir, len2, lob, clp, float3(zzonR.w, zzonG.w, zzonB.w)); // n

    // Normalize and dering.
    return min(max4, max(min4, aC * (1.0 / aW)));
}

float4 main(PSInput IN) : SV_Target
{
    // S24: SV_Position is in ABSOLUTE backbuffer pixel coordinates, not
    // viewport-relative ones - presentFinal()'s viewport (setPresentViewport())
    // is offset within the backbuffer to account for window chrome
    // (vp.TopLeftY = swap-chain height - world view rect top), so using
    // IN.position.xy directly as a 0-based output pixel index silently
    // shifted every sample by the chrome height (visible as a corrupted
    // band the size of the chrome bar). vary_fragcoord is the correct,
    // viewport-relative 0-1 coordinate instead - same GL-vs-D3D11 Y flip as
    // postDeferredNoDoFF.hlsl's tc (same vertex shader, same viewport, this
    // exact pass), then scaled to an output pixel position by out_screen_res.
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);
    float2 ip = tc * out_screen_res;

    float3 color = FsrEasuF(ip);

    // Alpha is passed through from a plain bilinear sample, untouched (same
    // intent as CASF.hlsl/RCASF.hlsl - this is a post-tonemap composite
    // target, alpha isn't part of EASU's own resampling). This is an
    // upscale, so ip (an OUTPUT pixel position) is NOT a valid coordinate
    // into the smaller source texture - it has to go through the same con0
    // (output-pixel -> input-texel) and con1 (input-texel -> normalized UV,
    // adding the +0.5 texel-center offset) transform FsrEasuF() uses
    // internally for its own taps.
    float2 srcTexel = ip * asfloat(easu_const0.xy) + asfloat(easu_const0.zw);
    float2 srcUV = (srcTexel + 0.5) * asfloat(easu_const1.xy);
    float alpha = diffuseRect.SampleLevel(diffuseRectSampler, srcUV, 0).a;

    return float4(color, alpha);
}
#endif
