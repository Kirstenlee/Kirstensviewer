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
// Second sprite channel: lldrawpoolwlsky.cpp binds getBloomTex()/
// getBloomTexNext() to units 0/1 with a real blend_factor for windlight
// sky-preset transitions. When only one texture is bound, unit 1 falls back
// to the white texture but blend_factor is also forced to 0 in that case.
Texture2D nextDiffuseMap : register(t1);
SamplerState nextDiffuseMapSampler : register(s1);
uniform float blend_factor;
uniform float custom_alpha;
uniform float time;
// Nebula-only daylight gate, distinct from custom_alpha/daylight_factor
// (which drive the point-star field) - see the is_nebula branch below.
uniform float sun_elevation;

// KVTweaks-exposed night-sky controls - see dxdrawpoolwlsky.cpp for the
// uniform1f() call sites and RenderStarGlow/RenderStarDensity/
// RenderNebulaEnabled/RenderNebulaIntensity in settings.xml.
uniform float star_glow;
uniform float star_density;
uniform float nebula_enabled;
uniform float nebula_intensity;

// RenderSkyStyle: 0=Default, 1=Real Constellations (llvowlsky.cpp placement
// only, no shader change), 2=Starry Night (blue/gold duotone + swirl
// distortion, this file). See settings.xml / dxdrawpoolwlsky.cpp's uniform1f().
uniform float sky_style;

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
// SV_Position required on bare-Varying PS inputs - see uiF.hlsl's comment.
struct PSInput
{
    float4 position : SV_Position;
    StarsVarying varying;
};

// Endpoint colors are pushed saturated (near-white reads unclear once
// blended) with white kept to a narrow middle band rather than half the range.
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

// Nebula patches (last NUM_NEBULA_PATCHES slots of the baked star field,
// flagged via vertex_color.g==0 - see llvowlsky.cpp's initStars()) pick one
// of three emission/reflection-nebula palettes off the shared star_seed hash.
float3 nebulaColorFromSeed(float t)
{
    float3 c_emission   = float3(0.95, 0.15, 0.10); // red (H-alpha emission)
    float3 c_reflection = float3(0.30, 0.55, 0.95); // blue-teal (dust scatter)
    float3 c_planetary  = float3(0.20, 0.95, 0.35); // green (O-III glow)

    if (t < 0.34)      return c_emission;
    else if (t < 0.67) return c_reflection;
    else               return c_planetary;
}

// Starry Night style: a deliberately limited, exaggerated blue/gold duotone
// in place of the naturalistic temperature palette above.
float3 starryNightColorFromSeed(float t)
{
    float3 c_deep_blue = float3(0.10, 0.18, 0.55);
    float3 c_cyan      = float3(0.25, 0.55, 0.80);
    float3 c_gold      = float3(1.00, 0.82, 0.30);
    float3 c_white_hot = float3(1.00, 0.96, 0.75);

    if (t < 0.40)      return lerp(c_deep_blue, c_cyan,     t / 0.40);
    else if (t < 0.75) return lerp(c_cyan,      c_gold,     (t - 0.40) / 0.35);
    else               return lerp(c_gold,      c_white_hot,(t - 0.75) / 0.25);
}

// Outward radial "sonar ping" wave: `phase` decreases with time and
// increases with radius, so lines of constant phase move outward as time
// passes, each ring born at the center and fading before the next begins -
// a pure function of time+radius, no state to track. Contrast tapers to 0
// at the edge so a ring dissipates as it grows. Returns ~[1-contrast,
// 1+contrast] as a direct brightness multiplier. `contrast_falloff` controls
// how far rings persist before fading; lower `freq` gives fewer, wider-spaced rings.
float ringPulse(float2 uv, float freq, float speed, float phase_offset, float contrast_falloff)
{
    float2 centered = uv - 0.5;
    float radius = length(centered) * 2.0;
    float phase = radius * freq - time * speed + phase_offset;
    float wave = cos(phase * 6.2831853);
    float contrast = saturate(1.0 - radius * contrast_falloff);
    return 1.0 + wave * contrast;
}

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    float seed = IN.varying.star_seed;

    // Nebula slots are flagged via vertex_color.g==0 (a real star's green
    // channel is hardcoded to 1.0 - see llvowlsky.cpp's initStars()). They're
    // oversized quads rendered as a soft radial-gradient color blob using
    // vary_texcoord0 directly, not the point-star sprite texture.
    bool is_nebula = IN.varying.vertex_color.g < 0.5;

    // Nebula gates on real sun elevation rather than custom_alpha
    // (daylight_factor, below): custom_alpha is the preset's artist-authored
    // Star Brightness curve, not an actual measure of whether the sun is up -
    // a bright moon can push it low enough to hide the nebula while the sun
    // is still well below the horizon. Point stars still use custom_alpha.
    float daylight_factor = smoothstep(0.0f, 0.9f, custom_alpha);
    float sun_elevation_factor = 1.0f - smoothstep(-0.05f, 0.15f, sun_elevation);

    if (is_nebula)
    {
        if (nebula_enabled < 0.5)
        {
            discard;
        }

        bool starry_night = sky_style > 1.5;

        float2 uv = IN.varying.vary_texcoord0.xy - 0.5;
        float dist = length(uv) * 2.0;
        float falloff = saturate(1.0 - dist);
        falloff = falloff * falloff * (3.0 - 2.0 * falloff); // smoothstep shape

        // Expanding concentric rings - see ringPulse()'s comment.
        float neb_ring = starry_night
            ? ringPulse(IN.varying.vary_texcoord0.xy, 5.0, 0.5, seed * 9.0, 0.9)
            : 1.0;

        // slow independent drift so nebula patches don't read as static
        // decals - two decorrelated sine terms, same spirit as the point-
        // star twinkle below.
        float drift = 0.85 + 0.15 * sin(time * 0.15 + seed * 17.0);

        // Pick TWO palette entries per blob off decorrelated seed hashes,
        // then mottle-blend between them across the quad's local UV (a cheap
        // 2D standing-wave interference pattern, not true noise) so the blob
        // reads as internal structure rather than a flat tinted disc.
        // Starry Night mode swaps the palette for the blue/gold star duotone.
        float3 color_a = starry_night
            ? starryNightColorFromSeed(frac(seed * 6.191))
            : nebulaColorFromSeed(frac(seed * 6.191));
        float3 color_b = starry_night
            ? starryNightColorFromSeed(frac(seed * 13.714 + 0.5))
            : nebulaColorFromSeed(frac(seed * 13.714 + 0.5));
        float mottle = sin(uv.x * 9.0 + seed * 23.0) * cos(uv.y * 7.0 + seed * 17.0);
        mottle = saturate(mottle * 0.65 + 0.5);
        float3 neb_color = lerp(color_a, color_b, mottle);

        float alpha = falloff * falloff * 0.35 * nebula_intensity * drift * neb_ring * sun_elevation_factor;

        OUT.data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        OUT.data2 = float4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

        float4 neb_out = float4(neb_color * alpha, alpha);
#if defined(HAS_EMISSIVE)
        OUT.data0 = float4(0, 0, 0, 0);
        OUT.data3 = neb_out;
#else
        OUT.data0 = neb_out;
#endif
        return OUT;
    }

    bool starry_night = sky_style > 1.5;

    // Constellation stars are flagged via VBLUE in [0.30,0.45] (every other
    // star's blue channel is always in [0.75,1.0] - see llvowlsky.cpp's
    // initStars()). Reconstruct a normal-looking blue tint from star_seed so
    // the flag has no visible effect on the star's actual rendered colour.
    bool is_constellation = IN.varying.vertex_color.b < 0.5;
    // VRED carries an authored spectral-type color_t for constellation stars
    // (llvowlsky.cpp's kConstellations data - e.g. Betelgeuse red, Rigel
    // blue) instead of the usual decorative near-white value - captured here
    // before vcol.r gets reconstructed to a neutral tint below, same
    // treatment as vcol.b just above.
    float authored_color_t = IN.varying.vertex_color.r;
    float3 vcol = IN.varying.vertex_color.rgb;
    if (is_constellation)
    {
        vcol.r = 1.0;
        vcol.b = 0.75 + frac(seed * 8.219) * 0.25;
    }

    // camera above water: class1\deferred\starsF.hlsl
    // camera below water: class1\environment\starsF.hlsl
    float4 col_a = diffuseMap.Sample(diffuseMapSampler, IN.varying.vary_texcoord0.xy);
    float4 col_b = nextDiffuseMap.Sample(nextDiffuseMapSampler, IN.varying.vary_texcoord0.xy);
    float4 col = lerp(col_a, col_b, blend_factor);

    // Two decorrelated sine octaves (slower base + faster detail layer, per
    // star) reshaped via pow() so brightness dwells near its peak and dips
    // quickly, floored above 0 so a star never fully turns off.
    float base_freq = lerp(2.2, 5.5, frac(seed * 7.1913));
    float base_phase = frac(seed * 13.377) * 6.2831853;
    float detail_freq = lerp(6.0, 11.0, frac(seed * 5.471));
    float detail_phase = frac(seed * 9.133) * 6.2831853;
    float wave = sin(time * base_freq + base_phase) * 0.65
               + sin(time * detail_freq + detail_phase) * 0.35;
    float twinkle = saturate(wave * 0.5 + 0.5);
    twinkle = pow(twinkle, 1.8);
    twinkle = lerp(0.45, 1.0, twinkle);

    // ~10% of stars are "flare" stars, given a bigger alpha boost so they
    // push harder into the existing bloom/glow threshold
    // (LLPipeline::generateGlow(), pipeline.cpp) instead of a hand-rolled
    // flare/diffraction-spike effect.
    float flare_roll = frac(seed * 4.129);
    float is_flare = step(0.90, flare_roll);
    // Under the default palette (starColorFromSeed, t=0 -> red) biasing
    // color_t toward 0 puts flare stars at the red end. starryNightColorFromSeed
    // has t=0 as deep blue instead, so Starry Night biases toward t near 1.0
    // (gold/white-hot) so flare stars don't drown out gold with blue.
    float color_t = starry_night
        ? lerp(frac(seed * 3.257), 0.82 + frac(seed * 3.257) * 0.18, is_flare)
        : lerp(frac(seed * 3.257), frac(seed * 3.257) * 0.34, is_flare);
    // Real Constellations: use the authored spectral-type color instead of the random
    // per-seed hue above - Betelgeuse should actually be red, not whatever the hash rolls.
    color_t = is_constellation ? authored_color_t : color_t;
    float flare_boost = 1.0 + is_flare * 2.5;

    // Starry Night mode swaps the naturalistic temperature palette for the
    // blue/gold duotone - see starryNightColorFromSeed().
    float3 star_tint = starry_night ? starryNightColorFromSeed(color_t) : starColorFromSeed(color_t);
    col.rgb *= star_tint * vcol;

    float factor = daylight_factor;

    // RenderStarDensity is a per-star visibility cull against star_seed -
    // 1.0 shows every baked star, lower values thin it out. Constellation
    // stars are exempt entirely (always visible regardless of density).
    float density_vis = is_constellation ? 1.0 : step(seed, star_density);

    // star_glow split in two ranges: 0-1 is a plain brightness scalar on
    // col.a directly (multiplying RGB channels that saturate at different
    // magnitudes collapses hue past ~1-2x, so pushing further does nothing
    // visible). Above 1.0 instead fades in a separate soft halo (same
    // UV-radial technique as the nebula blobs), hue-anchored to the star's
    // tint, whose RADIUS also grows with the slider.
    float glow_base = min(star_glow, 1.0);
    float glow_haze = max(star_glow - 1.0, 0.0);

    col.a = (col.a * factor) * 32.0f * flare_boost * glow_base;
    col.a *= twinkle * density_vis;

    // Falloff coefficients reach true 0 well inside the quad (~0.7-0.8),
    // leaving margin before the hard geometric edge so the pattern fades to
    // black instead of cutting off in a square silhouette; updateStarGeometry()
    // uses a bigger billboard to compensate so absolute visible size is
    // unchanged. Corona base brightness is kept low since RenderStarGlow=0
    // zeroes the base point but never gates the corona itself.
    if (starry_night)
    {
        float2 sn_uv = IN.varying.vary_texcoord0.xy - 0.5;
        float sn_radius = length(sn_uv) * 2.0;
        float sn_falloff = saturate(1.0 - sn_radius * 1.3);
        sn_falloff *= sn_falloff;

        float sn_ring = ringPulse(IN.varying.vary_texcoord0.xy, 2.0, 0.6, seed * 12.0, 1.2);
        col.a *= lerp(1.0, sn_ring, saturate(sn_radius * 0.8));

        float sn_corona_alpha = sn_falloff * sn_ring * 4.0 * flare_boost * twinkle * density_vis;
        col.rgb = lerp(col.rgb, star_tint * vcol, sn_falloff);
        col.a += sn_corona_alpha;
    }

    if (glow_haze > 0.0)
    {
        float2 haze_uv = IN.varying.vary_texcoord0.xy - 0.5;
        float haze_radius_boost = 1.0 + glow_haze * 0.18;
        float haze_dist = (length(haze_uv) * 2.0) / haze_radius_boost;
        float haze_falloff = saturate(1.0 - haze_dist);
        haze_falloff *= haze_falloff;

        float haze_alpha = haze_falloff * glow_haze * 0.4 * flare_boost * twinkle * density_vis;
        col.rgb = lerp(col.rgb, star_tint * IN.varying.vertex_color.rgb, haze_falloff);
        col.a += haze_alpha;
    }

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
