/**
 * @file class1/deferred/starsF.hlsl
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

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
// S24 (2026-08-29, task #279 "RENDER WOW"): this second texture channel was
// declared as a genuine feature on the C++ side all along -
// lldrawpoolwlsky.cpp binds LLVOSky::getBloomTex()/getBloomTexNext() to
// texture units 0/1 and computes a real blend_factor (windlight sky-preset
// transitions can use different star sprite assets) - but this file only
// ever declared/sampled register(t0), so col_a and col_b below were
// SAMPLING THE SAME TEXTURE at the same coordinates, making the lerp() a
// permanent no-op regardless of blend_factor. Fixed by actually sampling
// the second channel. When only one texture is bound, unit 1 falls back to
// the white texture (LLTexUnit::unbind()'s DX_RENDER behavior) but
// blend_factor is also forced to 0 by lldrawpoolwlsky.cpp in that case, so
// col_b's contribution is always fully excluded when it would otherwise be
// wrong - see that file's blend_factor=0.0f comments.
Texture2D nextDiffuseMap : register(t1);
SamplerState nextDiffuseMapSampler : register(s1);
uniform float blend_factor;
uniform float custom_alpha;
uniform float time;

#include "varying/starsVarying.hlsli"

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

// See:
// ALM off: class1/environment/starsF.hlsl
// ALM on : class1/deferred/starsF.hlsl
// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input.
struct PSInput
{
    float4 position : SV_Position;
    StarsVarying varying;
};

// S24 (task #279, round 2 - user feedback: "could be even more distinct
// like blue stars! and red giants"): pushed the endpoint colors themselves
// much more saturated (round 1's c_red/c_blue were too close to white to
// read clearly once blended) AND rebalanced the range so white is a
// narrower band in the middle rather than half the population - more of
// the population now lands on a visibly-tinted stop.
float3 starColorFromSeed(float t)
{
    float3 c_red    = float3(1.00, 0.18, 0.08); // red giant
    float3 c_orange = float3(1.00, 0.55, 0.18);
    float3 c_warm   = float3(1.00, 0.88, 0.72);
    float3 c_white  = float3(1.00, 1.00, 1.00);
    float3 c_blue   = float3(0.55, 0.72, 1.00); // hot blue-white

    if (t < 0.16)      return lerp(c_red,    c_orange, t / 0.16);
    else if (t < 0.34) return lerp(c_orange, c_warm,   (t - 0.16) / 0.18);
    else if (t < 0.55) return lerp(c_warm,   c_white,  (t - 0.34) / 0.21);
    else               return lerp(c_white,  c_blue,   (t - 0.55) / 0.45);
}

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // camera above water: class1\deferred\starsF.hlsl
    // camera below water: class1\environment\starsF.hlsl
    float4 col_a = diffuseMap.Sample(diffuseMapSampler, IN.varying.vary_texcoord0.xy);
    float4 col_b = nextDiffuseMap.Sample(nextDiffuseMapSampler, IN.varying.vary_texcoord0.xy);
    float4 col = lerp(col_a, col_b, blend_factor);

    float seed = IN.varying.star_seed;

    // S24 (task #279, round 2 - user feedback: "not twinkle, it's more
    // smooth turning on and off / a gentle pulse"): round 1 was a single
    // sine wave, which is smooth and symmetric by construction - reads as
    // breathing, not sparkle. Real atmospheric scintillation is faster and
    // less regular. Fixed by combining two decorrelated sine octaves (a
    // slower base + a faster detail layer, different frequency/phase per
    // star) then reshaping through pow() so the curve spends more time near
    // its peak and dips quickly rather than swinging symmetrically - and
    // floored well above 0 so a star never reads as fully "turning off",
    // just flickering in brightness.
    float base_freq = lerp(2.2, 5.5, frac(seed * 7.1913));
    float base_phase = frac(seed * 13.377) * 6.2831853;
    float detail_freq = lerp(6.0, 11.0, frac(seed * 5.471));
    float detail_phase = frac(seed * 9.133) * 6.2831853;
    float wave = sin(time * base_freq + base_phase) * 0.65
               + sin(time * detail_freq + detail_phase) * 0.35;
    float twinkle = saturate(wave * 0.5 + 0.5);
    twinkle = pow(twinkle, 1.8);
    twinkle = lerp(0.45, 1.0, twinkle);

    // S24 (task #279, round 2 - "more flare and bloom"): ~10% of stars are
    // "flare" stars - biased toward the red/orange end of the palette (real
    // red giants: rare but among the brightest naked-eye stars) and given a
    // much bigger alpha boost so they push harder into the existing bloom/
    // glow threshold (LLPipeline::generateGlow(), pipeline.cpp - unmodified,
    // this just feeds it a stronger source) rather than needing a hand-
    // rolled flare/diffraction-spike effect of their own.
    float flare_roll = frac(seed * 4.129);
    float is_flare = step(0.90, flare_roll);
    float color_t = lerp(frac(seed * 3.257), frac(seed * 3.257) * 0.34, is_flare);
    float flare_boost = 1.0 + is_flare * 2.5;

    float3 star_tint = starColorFromSeed(color_t);
    // Stardust/galactic band: stars inside it lean blue-white (young, hot
    // stars cluster along a real galactic plane) and burn brighter/denser-
    // looking, without needing a separate nebula texture or fullscreen pass.
    float3 band_tint = lerp(star_tint, float3(0.80, 0.88, 1.00), IN.varying.galactic_band * 0.6);
    col.rgb *= band_tint * IN.varying.vertex_color.rgb;

    float factor = smoothstep(0.0f, 0.9f, custom_alpha);
    float density_boost = 1.0 + IN.varying.galactic_band * 0.8;

    col.a = (col.a * factor) * 32.0f * density_boost * flare_boost;
    col.a *= twinkle;

    OUT.data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    OUT.data2 = float4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data3 = col;
#else
    OUT.data0 = col;
#endif

    return OUT;
}
