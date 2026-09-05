/**
 * @file class1/deferred/tonemapUtilF.hlsl
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

// S24 (2026-08-09, task #164): this file was previously an incomplete port -
// only toneMapACES_Hill existed, hardcoded as the sole result of toneMap(),
// with no exposure handling, no tonemap_mix blend, and no tonemap_type
// selector - meaning DX_RENDER never actually read the RenderTonemapType
// preference (Khronos/ACES/Hable/Uchimura) at all. Confirmed via direct
// comparison against tonemapUtilF.glsl's real toneMap()/toneMapNoExposure(),
// which switch on tonemap_type and blend via tonemap_mix. Real root cause of
// task #164's "disable haze -> pure white sky": DX_RENDER's present chain
// was calling postDeferredGammaCorrect.hlsl (linear_to_srgb + hard clamp,
// no tonemap curve at all) instead of postDeferredTonemap.hlsl - see
// dxpipeline.cpp's presentDeferredScreen() for the wiring fix.
//
// exposureMap - new register. t8/s8 chosen as the one slot free across
// every consumer of this shared utility file: water (waterF.hlsl uses
// t4-t7 for exclusion/bump/screen, t9/s9 for environmentMap via
// reflectionProbeF.hlsl) and the post-process tonemap family
// (postDeferredTonemap.hlsl, isDeferred=true so t0-t3/s0-s3 are reserved
// by deferredUtil.hlsl, and its own diffuseRect now lives at t7/s7 to
// match postDeferredGammaCorrect.hlsl's established convention).
Texture2D exposureMap : register(t8);
SamplerState exposureMapSampler : register(s8);

uniform float exposure;
uniform float tonemap_mix;
uniform int tonemap_type;

//===============================================================
// tone mapping taken from Khronos sample implementation
//===============================================================

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat = float3x3
(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
);


// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat = float3x3
(
    1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
);

// ACES tone map (faster approximation)
// see: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 toneMapACES_Narkowicz(float3 color)
{
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;
    return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
}


// ACES filmic tone map approximation
// see https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
float3 RRTAndODTFit(float3 color)
{
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return a / b;
}


// tone mapping
float3 toneMapACES_Hill(float3 color)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = clamp(color, 0.0, 1.0);

    return color;
}

// Khronos Neutral tonemapping
// https://github.com/KhronosGroup/ToneMapping/tree/main
// Input color is non-negative and resides in the Linear Rec. 709 color space.
// Output color is also Linear Rec. 709, but in the [0, 1] range.
//
// S24 (2026-08-19, task #233, task #227 audit finding): the body below was
// a completely different algorithm (a luminance-dot-product compression
// curve) with no relation to the real Khronos PBR Neutral formula GLSL
// uses (min-channel offset + peak-based compression + desaturation mix) -
// not a simplified port, an unrelated substitute. This is tonemap_type 0,
// the DEFAULT, so every user on the default (or explicitly Khronos-Neutral)
// RenderTonemapType got a materially different tonemap curve under
// DX_RENDER for every frame. Re-ported faithfully from tonemapUtilF.glsl's
// real PBRNeutralToneMapping() below.
float3 PBRNeutralToneMapping(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak * float3(1.0, 1.0, 1.0), g);
}

// Hable (Uncharted 2) tonemap operator
// see: http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 toneMapHable_Partial(float3 x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 toneMapHable(float3 color)
{
    const float exposureBias = 2.0;
    float3 curr = toneMapHable_Partial(color * exposureBias);
    float3 whiteScale = 1.0 / toneMapHable_Partial(float3(11.2, 11.2, 11.2));
    return curr * whiteScale;
}

// Uchimura tonemap operator
// see: https://www.desmos.com/calculator/gslcdxvipg
float3 toneMapUchimura(float3 x)
{
    const float P = 1.0;  // max display brightness
    const float a = 1.0;  // contrast
    const float m = 0.22; // linear section start
    const float l = 0.4;  // linear section length
    const float c = 1.33; // black
    const float b = 0.0;  // pedestal

    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float3 w0 = 1.0 - smoothstep(0.0, m, x);
    float3 w2 = step(S0, x);
    float3 w1 = 1.0 - w0 - w2;

    // S24 (2026-09-02): was pow(x/m, ...) unguarded - x is the input HDR
    // color, which should be non-negative in principle but isn't
    // guaranteed to stay exactly so under floating-point drift from
    // upstream lighting math. pow() with a negative base and non-integer
    // exponent is undefined behavior, and D3D11 is more likely to reliably
    // return NaN for it than to silently degrade - a NaN here would
    // poison this whole pixel's final tonemapped color, not just look
    // slightly off. Same established fix already used for this exact
    // problem in atmosphericsFuncs.hlsl's calcAtmosphericVars() (see its
    // own comment) - wrap with abs() rather than leave unguarded.
    float3 T = m * pow(abs(x / m), float3(c, c, c)) + b;
    float3 S = P - (P - S1) * exp(CP * (x - S0));
    float3 L = m + a * (x - m);

    return T * w0 + L * w1 + S * w2;
}

float3 toneMap(float3 color)
{
#ifndef NO_POST
    float3 linear_input_color = color;

    float exp_scale = exposureMap.SampleLevel(exposureMapSampler, float2(0.5, 0.5), 0).r;
    float final_exposure = exposure * exp_scale;
    float3 exposed_color = color * final_exposure;

    float3 tonemapped_color = exposed_color;
    switch (tonemap_type)
    {
    case 0:
        tonemapped_color = PBRNeutralToneMapping(exposed_color);
        break;
    case 1:
        tonemapped_color = toneMapACES_Hill(exposed_color);
        break;
    case 2:
        tonemapped_color = toneMapHable(exposed_color);
        break;
    case 3:
        tonemapped_color = toneMapUchimura(exposed_color);
        break;
    }

    float3 exposed_linear_input = linear_input_color * final_exposure;
    color = lerp(exposed_linear_input, tonemapped_color, tonemap_mix);

    color = clamp(color, 0.0, 1.0);
#else
    color *= exposure * exposureMap.SampleLevel(exposureMapSampler, float2(0.5, 0.5), 0).r;
    color = clamp(color, 0.0, 1.0);
#endif

    return color;
}

float3 toneMapNoExposure(float3 color)
{
#ifndef NO_POST
    float3 linear_input_color = color;

    float3 tonemapped_color = color;
    switch (tonemap_type)
    {
    case 0:
        tonemapped_color = PBRNeutralToneMapping(color);
        break;
    case 1:
        tonemapped_color = toneMapACES_Hill(color);
        break;
    case 2:
        tonemapped_color = toneMapHable(color);
        break;
    case 3:
        tonemapped_color = toneMapUchimura(color);
        break;
    }

    color = lerp(linear_input_color, tonemapped_color, tonemap_mix);

    color = clamp(color, 0.0, 1.0);
#else
    color = clamp(color, 0.0, 1.0);
#endif

    return color;
}
