/**
 * @file class1/deferred/postDeferredGammaCorrect.hlsl
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

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (attached below, isDeferred=true
// on both gDeferredPostGammaCorrectProgram/gLegacyPostGammaCorrectProgram) -
// moved this file's own texture to t7/s7, matching the established
// free-slot convention used elsewhere in this project.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

uniform float gamma;

// screen_res is also declared (and used) by deferredUtil.hlsl - this copy
// is dead in both the original GLSL and this port (declared, never
// referenced - confirmed via grep of both postDeferredGammaCorrect.glsl
// and this file). Deleted, not guarded.

float3 linear_to_srgb(float3 cl);
float getDepth(float2 pos_screen);
float interleavedGradientNoise(float2 pixel_pos);

// S24: same fake-godrays mechanism as postDeferredTonemap.hlsl - see its comment. Duplicated
// rather than shared since this program doesn't attach tonemapUtilF.hlsl, same reasoning as the
// dither function below.
uniform float2 sun_screen_pos;
uniform float godray_intensity;
uniform float3 sun_tint_color;
// S24: see postDeferredTonemap.hlsl's identical comment - narrows the march radius as the camera
// swings away from the sun. 1.0 = full radius (looking straight at the sun).
uniform float godray_spread;

float3 applyGodrays(float2 pos_screen, float2 pixel_pos, float3 base_color)
{
    if (godray_intensity <= 0.0)
    {
        return base_color;
    }

    // S24: see postDeferredTonemap.hlsl's identical comment - GPU Gems 3 ch.13 "Volumetric Light
    // Scattering as a Post-Process" (Mitchell). March the REAL distance to the sun over a fixed
    // sample count - a fixed local radius (what this used to do) can only ever produce a flat
    // blob around the sun, never a directional ray (confirmed live, godzero/godfull.png).
    static const int kGodraySamples = 40;
    static const float kWeight = 1.0 / (float)kGodraySamples;
    static const float kDecay = 0.92;

    // S24: "density" compresses the march to cover less than the full pixel-to-sun distance in
    // kGodraySamples steps - godray_spread (CPU-computed off-axis angle, see dxpipeline.cpp)
    // drives this directly: full reach looking straight at the sun, denser/shorter rays toward
    // grazing angles.
    static const float kBaseDensity = 0.55;
    static const float kDensityBoost = 0.35;
    float density = kBaseDensity + (1.0 - godray_spread) * kDensityBoost;

    float2 to_sun = sun_screen_pos - pos_screen;
    float dist_to_sun = length(to_sun);
    float2 dir_to_sun = dist_to_sun > 0.0001 ? to_sun / dist_to_sun : float2(0.0, 0.0);
    float2 delta = to_sun * (density / (float)kGodraySamples);
    // S24: see postDeferredTonemap.hlsl's identical comment - dither the march's starting
    // phase per-pixel so hard occluder edges blur into a smooth streak instead of leaving
    // discrete ghosted copies at each step.
    float2 sample_pos = pos_screen + delta * interleavedGradientNoise(pixel_pos);
    float illumination_decay = 1.0;
    float accum_weight = 0.0;

    // S24: see postDeferredTonemap.hlsl's identical comment - soft occlusion edges via a real
    // multi-tap spatial blur, not noise dithering (the perpendicular jitter this used to have
    // read as visible dither speckle on hard silhouette edges - bandyray.png).
    static const int kOcclusionTaps = 4;
    static const float kOcclusionBlurRadius = 0.02;
    float2 perp_dir = float2(-dir_to_sun.y, dir_to_sun.x);
    float2 occ_offsets[4] =
    {
        perp_dir * kOcclusionBlurRadius,
        -perp_dir * kOcclusionBlurRadius,
        dir_to_sun * kOcclusionBlurRadius,
        -dir_to_sun * kOcclusionBlurRadius
    };

    for (int i = 0; i < kGodraySamples; ++i)
    {
        sample_pos += delta;
        // S24: see postDeferredTonemap.hlsl's identical comment - exact depth<=0.0 check
        // (same convention as krlvCamDrawFogF.hlsl), not a loose epsilon. Confirmed live
        // (wildray.png): an epsilon classified real distant water as "sky" too, lighting up
        // a whole horizon-wide band instead of localized rays.
        float depth = getDepth(sample_pos);
        float sky_mask = depth <= 0.0 ? 1.0 : 0.0;
        [unroll]
        for (int t = 0; t < kOcclusionTaps; ++t)
        {
            float tap_depth = getDepth(sample_pos + occ_offsets[t]);
            sky_mask += tap_depth <= 0.0 ? 1.0 : 0.0;
        }
        sky_mask /= (float)(kOcclusionTaps + 1);
        // S24: occlusion-only accumulation, not the scene color buffer - see
        // postDeferredTonemap.hlsl's identical comment (confirmed live: sampling diffuseRect
        // here smeared every star in the night sky into its own streak).
        accum_weight += sky_mask * illumination_decay * kWeight;
        illumination_decay *= kDecay;
    }

    // S24: see postDeferredTonemap.hlsl's identical comment - cloud-reactivity attempt tried
    // and reverted here, parked rather than iterated further live.
    return base_color + sun_tint_color * accum_weight * godray_intensity;
}

struct PSInput
{
    // S24: missing SV_Position shifts every subsequent semantic register by one relative to the VS output.
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

float3 legacyGamma(float3 color)
{
    float3 c = 1.0 - clamp(color, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    c = 1.0 - pow(c, float3(gamma, gamma, gamma));
    return c;
}

// S24: same ordered-dither fix as postDeferredTonemap.hlsl - see its comment for the mechanism.
// Duplicated rather than shared since this program doesn't attach tonemapUtilF.hlsl.
float interleavedGradientNoise(float2 pixel_pos)
{
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixel_pos, magic.xy)));
}

float4 main(PSInput IN) : SV_Target
{
    // S24: GL-vs-D3D11 texture-origin flip — vary_fragcoord itself stays in GL's original convention (postDeferredNoTCV.hlsl); the flip belongs here, at the Sample() call site.
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);
    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc);
    diff.rgb = applyGodrays(IN.vary_fragcoord.xy, IN.position.xy, diff.rgb);
    diff.rgb = linear_to_srgb(diff.rgb);
#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif
    diff.rgb += (interleavedGradientNoise(IN.position.xy) - 0.5) / 255.0;
    diff.rgb = clamp(diff.rgb, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    return diff;
}
