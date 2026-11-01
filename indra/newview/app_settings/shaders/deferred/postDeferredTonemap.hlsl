/**
 * @file /deferred/postDeferredTonemap.hlsl
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

// S24: t0-t3/s0-s3 reserved by deferredUtil.hlsl (mFeatures.isDeferred=true on every program using this file), same convention as postDeferredGammaCorrect.hlsl's diffuseRect (t7/s7). exposureMap (tonemapUtilF.hlsl, mFeatures.hasTonemap) uses t8/s8.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

struct PSInput
{
    // S24: missing SV_Position shifts every subsequent semantic register by one relative to the VS output.
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

#ifdef GAMMA_CORRECT
uniform float gamma;
#endif

float3 linear_to_srgb(float3 cl);
float3 toneMap(float3 color);
float3 clampHDRRange(float3 color);
float getDepth(float2 pos_screen);
float interleavedGradientNoise(float2 pixel_pos);

// S24: fake godrays - sun position in screen UV (GL convention), may land
// outside [0,1] when off-screen (C++ clamps toward the visible edge). 0
// disables the effect with no cost beyond the one branch.
uniform float2 sun_screen_pos;
uniform float godray_intensity;
uniform float3 sun_tint_color;
// S24: narrows the march radius as the camera swings away from the sun -
// see dxpipeline.cpp. 1.0 = full radius (looking straight at the sun).
uniform float godray_spread;

// S24: cheap fake crepuscular rays, GPU Gems 3 ch.13 "Volumetric Light
// Scattering as a Post-Process" (Mitchell) - march the REAL distance from
// this pixel to the sun's screen position (a fixed local radius instead
// produces a flat blob, not a directional ray - don't go back to that).
// Applied pre-tonemap so added brightness compresses with everything else.
float3 applyGodrays(float2 pos_screen, float2 pixel_pos, float3 base_color)
{
    if (godray_intensity <= 0.0)
    {
        return base_color;
    }

    // Sample count must stay fine enough not to alias against evenly-spaced
    // foreground detail when marching the full distance.
    static const int kGodraySamples = 40;
    static const float kWeight = 1.0 / (float)kGodraySamples;
    // S24: kDecay^i is the real range-limiting mechanism (not a hard
    // cutoff) - by sample 40 contribution is ~3.6% of the first sample's.
    static const float kDecay = 0.92;

    // S24: "density" (paper's term) compresses the march below the full
    // pixel-to-sun distance - godray_spread (camera angle off the sun,
    // C++-computed) drives it: full reach straight at the sun, denser/
    // shorter rays at grazing angles.
    static const float kBaseDensity = 0.55;
    static const float kDensityBoost = 0.35;
    float density = kBaseDensity + (1.0 - godray_spread) * kDensityBoost;

    float2 to_sun = sun_screen_pos - pos_screen;
    float dist_to_sun = length(to_sun);
    float2 dir_to_sun = dist_to_sun > 0.0001 ? to_sun / dist_to_sun : float2(0.0, 0.0);
    float2 delta = to_sun * (density / (float)kGodraySamples);
    // S24: dither the march's start phase per-pixel (IGN hash) - otherwise
    // every pixel samples occluders at identical offsets, producing
    // ghosted bands off hard-edged occluders instead of a smooth streak.
    float2 sample_pos = pos_screen + delta * interleavedGradientNoise(pixel_pos);
    float illumination_decay = 1.0;
    float accum_weight = 0.0;

    // S24: occlusion edges softened via real depth-tap blur, not noise
    // jitter (jitter reads as dither speckle on hard silhouettes). Trimmed
    // to the 2 perpendicular taps (dropped the along-ray pair, which blur
    // in the same direction the march already samples) for perf - 40
    // samples x 3 fetches instead of x5.
    static const int kOcclusionTaps = 2;
    static const float kOcclusionBlurRadius = 0.02;
    float2 perp_dir = float2(-dir_to_sun.y, dir_to_sun.x);
    float2 occ_offsets[2] =
    {
        perp_dir * kOcclusionBlurRadius,
        -perp_dir * kOcclusionBlurRadius
    };

    for (int i = 0; i < kGodraySamples; ++i)
    {
        sample_pos += delta;
        // S24: reversed-Z - true sky reads exactly 0.0 (same convention as
        // krlvCamDrawFogF.hlsl). A looser epsilon here once misclassified
        // distant real geometry (water toward the horizon) as sky.
        float depth = getDepth(sample_pos);
        float sky_mask = depth <= 0.0 ? 1.0 : 0.0;
        [unroll]
        for (int t = 0; t < kOcclusionTaps; ++t)
        {
            float tap_depth = getDepth(sample_pos + occ_offsets[t]);
            sky_mask += tap_depth <= 0.0 ? 1.0 : 0.0;
        }
        sky_mask /= (float)(kOcclusionTaps + 1);
        // S24: occlusion-only accumulation, not the scene color buffer -
        // sampling the color buffer here smears every bright point (stars)
        // along the march into its own streak.
        accum_weight += sky_mask * illumination_decay * kWeight;
        illumination_decay *= kDecay;
    }

    // S24: cloud-reactivity (dimming rays under cloud cover) was tried and
    // reverted - clouds are a 2.5D trick with no real depth to test
    // against, so the effect just popped unpredictably. Not revisited.
    return base_color + sun_tint_color * accum_weight * godray_intensity;
}

// S24: ordered dither (Jorge Jimenez's Interleaved Gradient Noise), applied right before the
// final 8-bit quantize below - the swapchain backbuffer is DXGI_FORMAT_R8G8B8A8_UNORM with no
// dither anywhere in the pipeline, so a long, low-contrast gradient (e.g. a low sun reflected
// across water toward the horizon) quantizes into visible bands; a midday sky's steeper gradient
// masks the same 8-bit steps. +-0.5 LSB randomizes which way each pixel rounds instead.
float interleavedGradientNoise(float2 pixel_pos)
{
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixel_pos, magic.xy)));
}

#ifdef GAMMA_CORRECT
float3 legacyGamma(float3 color)
{
    float3 c = 1.0 - clamp(color, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    c = 1.0 - pow(c, float3(gamma, gamma, gamma));
    return c;
}
#endif

float4 main(PSInput IN) : SV_Target
{
    // S24: GL-vs-D3D11 texture-origin flip, same convention as postDeferredGammaCorrect.hlsl.
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);
    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc);
    diff.rgb = applyGodrays(IN.vary_fragcoord.xy, IN.position.xy, diff.rgb);
#ifndef NO_POST
    diff.rgb = toneMap(diff.rgb);
#else
    diff.rgb = clamp(diff.rgb, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
#endif
#ifdef GAMMA_CORRECT
    diff.rgb = linear_to_srgb(diff.rgb);
#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif
#endif
    diff.rgb += (interleavedGradientNoise(IN.position.xy) - 0.5) / 255.0;
    diff.rgb = clamp(diff.rgb, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    return diff;
}
