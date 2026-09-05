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

// S24 (task #279 stage 2, "RENDER WOW"): KVTweaks-exposed night-sky
// controls - see dxdrawpoolwlsky.cpp for the uniform1f() call sites and
// RenderStarGlow/RenderStarDensity/RenderStarDustIntensity/
// RenderNebulaEnabled/RenderNebulaIntensity in settings.xml.
uniform float star_glow;
uniform float star_density;
uniform float star_dust_intensity;
uniform float nebula_enabled;
uniform float nebula_intensity;

// S24 (task #302, "Starry Night" sky style, 2026-08-31): 0=Default,
// 1=Real Constellations (llvowlsky.cpp placement only, no shader change),
// 2=Starry Night (blue/gold duotone + swirl distortion, this file).
// See RenderSkyStyle in settings.xml / dxdrawpoolwlsky.cpp's uniform1f().
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

// S24 (task #279 stage 2): nebula patches (the last NUM_NEBULA_PATCHES
// slots of the baked star field, flagged via vertex_color.g==0 - see
// llvowlsky.cpp's initStars()) pick one of three tasteful emission/
// reflection-nebula palettes off the same star_seed hash everything else
// here already uses.
float3 nebulaColorFromSeed(float t)
{
    float3 c_emission   = float3(0.95, 0.15, 0.10); // red (H-alpha emission)
    float3 c_reflection = float3(0.30, 0.55, 0.95); // blue-teal (dust scatter)
    float3 c_planetary  = float3(0.20, 0.95, 0.35); // green (O-III glow)

    if (t < 0.34)      return c_emission;
    else if (t < 0.67) return c_reflection;
    else               return c_planetary;
}

// S24 (task #302, "Starry Night" sky style): user's ask was "based on the
// general star field, just fantastic blue and yellow swirly effects, a
// stylised version" - a deliberately limited, exaggerated duotone in place
// of the naturalistic temperature palette above (Van Gogh's sky uses far
// fewer, far more saturated hues than a realistic starfield).
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

// S24 (task #302, 2nd revision - user: "perhaps rather than swirls,
// concentric rings growing in size and fading as they grow, forever
// cyclic"): a classic "sonar ping" outward radial wave. `phase` decreases
// with time and increases with radius, so lines of constant phase (the
// rings themselves) physically move outward as time passes - each ring is
// born at the center, expands, and fades before the next begins, forever,
// with no start/end state to track (pure function of time+radius). Contrast
// is highest near the center and tapers to 0 by the edge, so a ring
// visibly dissipates as it grows rather than remaining sharp all the way
// out. Returns roughly [1-contrast, 1+contrast], meant as a direct
// brightness multiplier.
// S24 (task #302, 3rd revision, user: "we are being way too conservative,
// need multiple rings bigger spacing radiating out in huge spans, this has
// to be not so realistic and more artistic"): `contrast_falloff` controls
// how far the rings persist before fading (lower = rings stay visible much
// further from center, for a big dramatic span); `freq` lower = fewer,
// wider-spaced rings instead of a tight repeating pattern.
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

    // S24 (task #279 stage 2): nebula slots are flagged via vertex_color.g
    // ==0 (a real star's green channel is hardcoded to 1.0 and never
    // touched again after init - see llvowlsky.cpp's initStars()). They're
    // deliberately oversized quads (updateStarGeometry()) rendered as a
    // soft radial-gradient color blob using vary_texcoord0 directly,
    // instead of sampling the point-star sprite texture at all.
    bool is_nebula = IN.varying.vertex_color.g < 0.5;

    // S24 (2026-09-04): user report - nebula/shooting stars stayed fully
    // visible in broad daylight while point stars correctly faded via
    // custom_alpha ("factor" below) - moved that same fade curve up here so
    // both branches share it, matching the user's expectation that nebula
    // "behave in the same manner as stars do."
    float daylight_factor = smoothstep(0.0f, 0.9f, custom_alpha);

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

        // S24 (task #302, 2nd revision): expanding concentric rings - see
        // ringPulse()'s comment.
        float neb_ring = starry_night
            ? ringPulse(IN.varying.vary_texcoord0.xy, 5.0, 0.5, seed * 9.0, 0.9)
            : 1.0;

        // slow independent drift so nebula patches don't read as static
        // decals - two decorrelated sine terms, same spirit as the point-
        // star twinkle below.
        float drift = 0.85 + 0.15 * sin(time * 0.15 + seed * 17.0);

        // S24 (task #279 stage 2, user feedback: "LOVE the nebulas mixed
        // green and red as opposed to single colour blobs"): pick TWO
        // palette entries per blob off decorrelated seed hashes, then
        // mottle-blend between them across the quad's local UV (a cheap
        // 2D standing-wave interference pattern, not true noise, but reads
        // as genuine internal structure rather than a flat tinted disc).
        // S24 (task #302): Starry Night mode swaps the red/blue/green
        // nebula palette for the same blue/gold duotone used on the stars.
        float3 color_a = starry_night
            ? starryNightColorFromSeed(frac(seed * 6.191))
            : nebulaColorFromSeed(frac(seed * 6.191));
        float3 color_b = starry_night
            ? starryNightColorFromSeed(frac(seed * 13.714 + 0.5))
            : nebulaColorFromSeed(frac(seed * 13.714 + 0.5));
        float mottle = sin(uv.x * 9.0 + seed * 23.0) * cos(uv.y * 7.0 + seed * 17.0);
        mottle = saturate(mottle * 0.65 + 0.5);
        float3 neb_color = lerp(color_a, color_b, mottle);

        float alpha = falloff * falloff * 0.35 * nebula_intensity * drift * neb_ring * daylight_factor;

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

    // S24 (task #301 follow-up, user feedback: "they should ignore star
    // density altogether and be always visible irrespective of density"):
    // constellation stars are flagged via VBLUE in [0.30,0.45] (see
    // llvowlsky.cpp's initStars() comment - unambiguous, every other star's
    // blue channel is always in [0.75,1.0]). Reconstruct a normal-looking
    // blue tint from star_seed so the flag has no visible side effect on
    // the star's actual rendered colour.
    bool is_constellation = IN.varying.vertex_color.b < 0.5;
    float3 vcol = IN.varying.vertex_color.rgb;
    if (is_constellation)
    {
        vcol.b = 0.75 + frac(seed * 8.219) * 0.25;
    }

    // camera above water: class1\deferred\starsF.hlsl
    // camera below water: class1\environment\starsF.hlsl
    float4 col_a = diffuseMap.Sample(diffuseMapSampler, IN.varying.vary_texcoord0.xy);
    float4 col_b = nextDiffuseMap.Sample(nextDiffuseMapSampler, IN.varying.vary_texcoord0.xy);
    float4 col = lerp(col_a, col_b, blend_factor);

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
    // S24 (task #302 follow-up, root cause of "just a cool blue starfield"):
    // this bias compresses color_t toward 0 for flare stars, which under
    // the DEFAULT palette (starColorFromSeed, t=0 -> red) puts the
    // brightest/biggest stars at the red end as intended. But under
    // starryNightColorFromSeed, t=0 is DEEP BLUE - so every flare star
    // (extra-bright by design, flare_boost below) was being pushed toward
    // the coolest color, drowning out gold with the most visually dominant
    // stars in the scene. Starry Night mode biases the OTHER way instead,
    // toward the gold/white-hot end (t near 1.0) - matching the painting's
    // actual radiant golden stars - while the other 90% of the field still
    // spans the full blue->gold gradient at random, keeping the blue
    // population intact.
    float color_t = starry_night
        ? lerp(frac(seed * 3.257), 0.82 + frac(seed * 3.257) * 0.18, is_flare)
        : lerp(frac(seed * 3.257), frac(seed * 3.257) * 0.34, is_flare);
    float flare_boost = 1.0 + is_flare * 2.5;

    // S24 (task #302): Starry Night mode swaps the naturalistic temperature
    // palette for the blue/gold duotone - see starryNightColorFromSeed().
    float3 star_tint = starry_night ? starryNightColorFromSeed(color_t) : starColorFromSeed(color_t);
    // Stardust/galactic band: stars inside it lean blue-white (young, hot
    // stars cluster along a real galactic plane) and burn brighter/denser-
    // looking, without needing a separate nebula texture or fullscreen pass.
    // S24 (task #279 stage 2): "clump" gives the band visible texture
    // (patchy dust lane) instead of a smooth gradient, cheaply, off the same
    // per-star seed hash rather than a real 2D noise field; both clump and
    // the band's overall reach are user-scaled via star_dust_intensity.
    // S24 (task #279 stage 2, user feedback: "so faint as not to be
    // noticible"): both the tint blend and the brightness boost below were
    // capped well under their own visible range even at band==1 - raised
    // substantially so the band reads as an actual bright dust lane rather
    // than a faint tinge on a couple of stars.
    float clump = 0.55 + 0.75 * frac(seed * 41.719);
    float band = IN.varying.galactic_band * clump * star_dust_intensity;
    float3 band_tint = lerp(star_tint, float3(0.75, 0.85, 1.00), saturate(band * 1.4));
    col.rgb *= band_tint * vcol;

    float factor = daylight_factor;
    float density_boost = 1.0 + band * 2.5;

    // S24 (task #279 stage 2): RenderStarDensity is a pure per-star
    // visibility cull against star_seed - 1.0 shows every baked star (the
    // field's full NUM_REAL_STARS count, see llvowlsky.cpp), lower values
    // thin it out. No geometry/CPU cost, matches every other control here.
    // S24 (task #301 follow-up): constellation stars are exempt entirely -
    // the whole point of the mode is a readable, always-visible pattern.
    float density_vis = is_constellation ? 1.0 : step(seed, star_density);

    // S24 (task #279 stage 2, user feedback: "under 1.0 you get a better
    // distribution of colour, over that it blows everything out to white -
    // I was expecting more bloom/haze not less colour, value has not got
    // much value above 3.0"): the old scheme multiplied star_glow straight
    // into col.a, which multiplies the additive contribution of every RGB
    // channel together - since channels saturate to 1.0 (white) at
    // different magnitudes, pushing that product past ~1-2x collapses hue
    // entirely, and pushing it further does nothing MORE visible once every
    // channel is already clipped. Split it in two: 0-1 keeps the exact old
    // behaviour (a plain brightness scalar - this is the range the user
    // liked). Above 1.0 no longer brightens the point itself - it instead
    // fades in a wider, separate soft halo (same UV-radial technique as the
    // nebula blobs above), hue-anchored to the star's own tint rather than
    // the sprite texture, and its RADIUS - not just its intensity - grows
    // with the slider, so higher values genuinely keep doing more (spread
    // further) instead of asymptoting to a fully white core by ~2-3.
    float glow_base = min(star_glow, 1.0);
    float glow_haze = max(star_glow - 1.0, 0.0);

    col.a = (col.a * factor) * 32.0f * density_boost * flare_boost * glow_base;
    col.a *= twinkle * density_vis;

    // S24 (task #302, 4th revision, user: "better but the circles are
    // hitting the billboard... square needs to be bigger so the rings
    // dissapate into the night without clipping... even at glow 0.0 they
    // are BRIGHT they could do with less... the speed is just right just
    // need them to have the space to dissapate"): the previous pass's
    // falloff coefficients never actually reached 0 within the quad's own
    // radius range (max ~1.4 at the corners) - contrast/envelope were both
    // still positive right up to the hard geometric edge, so the pattern
    // visibly cut off in a square silhouette instead of fading to black.
    // Tightened both so they reach true 0 well inside the quad (~0.7-0.8),
    // leaving real margin before the edge; updateStarGeometry() compensates
    // with a bigger billboard so the ABSOLUTE visible size doesn't shrink,
    // it just gets genuine empty space to dissipate into. Ring speed/
    // frequency untouched per "the speed is just right". Corona base
    // brightness reduced (this is what's actually visible at
    // RenderStarGlow=0, since that setting zeroes the base point but never
    // gated the corona - see the col.a assignment above).
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
        col.rgb = lerp(col.rgb, band_tint * IN.varying.vertex_color.rgb, haze_falloff);
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
