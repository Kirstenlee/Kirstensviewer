/**
 * @file class1/deferred/galacticBandF.hlsl
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

uniform float star_dust_intensity; // reuses the existing KVTweaks "Galactic Dust Band" slider
uniform float sun_elevation;       // real sun elevation, same civil-twilight fade as starsShootingF.hlsl
uniform float time;
uniform float4x4 inv_proj;

// Fixed world-space band orientation, pre-rotated into VIEW space on the C++ side every frame
// (renderGalacticBandDeferred()) - "inv_modelview" isn't among the reserved uniforms llrender.cpp
// actually auto-syncs (only inv_proj is), so there's no way to recover world space in-shader.
uniform float3 galactic_normal_view;
uniform float3 band_u_view;
uniform float3 band_v_view;

#include "varying/galacticBandVarying.hlsli"

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

// SV_Position required on bare-Varying PS inputs - see uiF.hlsl's comment.
struct PSInput
{
    float4 position : SV_Position;
    GalacticBandVarying varying;
};

// Cheap deterministic 3D value-noise hash (same sin/frac trick starsV.hlsl's starHash() uses - no
// HLSL builtin noise()). 3D, not 2D - see main()'s comment on why "along the band" is embedded as
// a circle (cos/sin) rather than fed to the noise directly as a raw angle.
float hash31(float3 p)
{
    p = frac(p * float3(123.34, 456.21, 789.19));
    p += dot(p, p.yzx + 45.32);
    return frac((p.x + p.y) * p.z);
}

float valueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);

    float n000 = hash31(i + float3(0.0, 0.0, 0.0));
    float n100 = hash31(i + float3(1.0, 0.0, 0.0));
    float n010 = hash31(i + float3(0.0, 1.0, 0.0));
    float n110 = hash31(i + float3(1.0, 1.0, 0.0));
    float n001 = hash31(i + float3(0.0, 0.0, 1.0));
    float n101 = hash31(i + float3(1.0, 0.0, 1.0));
    float n011 = hash31(i + float3(0.0, 1.0, 1.0));
    float n111 = hash31(i + float3(1.0, 1.0, 1.0));

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);

    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);

    return lerp(nxy0, nxy1, u.z);
}

// 4-octave fractal Brownian motion - the actual "cloudy dust lane" texture. Amplitude halves and
// frequency roughly doubles each octave, the standard fBm recipe for organic-looking noise.
float fbm(float3 p)
{
    float v = 0.0;
    float amp = 0.5;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        v += amp * valueNoise(p);
        p = p * 2.03 + 11.7;
        amp *= 0.5;
    }
    return v;
}

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // Reconstruct this pixel's real camera-ray direction, VIEW space - no dome mesh, no depth
    // sample needed (direction only, magnitude is irrelevant). ndc.z picks an arbitrary point
    // along the ray (any non-degenerate value works, direction is unaffected).
    float4 view_pos = mul(inv_proj, float4(IN.varying.ndc_xy, 1.0, 1.0));
    float3 dir = normalize(view_pos.xyz / view_pos.w);

    // galactic_normal_view/band_u_view/band_v_view are the band's fixed world-space orientation,
    // pre-rotated into this frame's view space on the C++ side (renderGalacticBandDeferred()) -
    // see this file's top-of-file comment for why.

    // Smooth, singularity-free everywhere on the sphere (a plain dot product - no pole, no wrap) -
    // this is the entire reason this approach replaces the equirectangular photo attempt.
    float band_dist = dot(dir, galactic_normal_view);

    // Soft Gaussian-ish cross-section, asymmetric width from the plane's center - real Milky Way
    // photos show a fairly tight core band with a wide, slowly-fading halo either side.
    float band_shape = exp(-band_dist * band_dist * 18.0) * 0.7
                      + exp(-band_dist * band_dist * 3.5) * 0.3;

    // "along the band" wraps at +-pi (a plain atan2) - feeding that angle straight into the noise
    // as a coordinate looked fine in theory (hash noise is chaotic, so the seam should be just
    // another random jump) but was VISIBLE in practice as a single hard edge at one fixed compass
    // heading: value noise isn't periodic in its input, so the samples at along=-pi and along=+pi
    // are two uncorrelated random values sitting right next to each other on screen, not a smooth
    // continuation. Fixed by embedding "along" on a circle (cos/sin) instead of using the raw
    // angle - cos/sin are exactly periodic, so this is guaranteed seamless everywhere, not just
    // "probably fine": the noise never sees a value of "along" itself, only where that angle sits
    // on a circle, which is the same point whether approached from -pi or +pi.
    float along = atan2(dot(dir, band_v_view), dot(dir, band_u_view));
    float across = band_dist;

    // Slow drift as an actual rotation of the angle before the circle embedding - keeps the
    // seamless property exactly, forever, instead of nudging the embedded coordinates off the
    // circle (which a plain "+= time" on the noise input would do).
    float along_drift = along + time * 0.004;
    float2 circle = float2(cos(along_drift), sin(along_drift)) * 3.2;
    float3 noise_p = float3(circle, across * 7.0);

    float density = fbm(noise_p) * 0.65 + fbm(noise_p * 2.3 + 31.1) * 0.35;
    // Push contrast so it reads as patchy dust lanes, not a flat haze.
    density = saturate((density - 0.3) * 1.8);

    float sun_elevation_factor = 1.0 - smoothstep(-0.05, 0.15, sun_elevation);

    float alpha = band_shape * density * star_dust_intensity * sun_elevation_factor * 0.9;

    // Neutral cool-white/blue-grey dust tone, warming very slightly toward the band's core.
    float3 col = lerp(float3(0.55, 0.62, 0.75), float3(0.80, 0.80, 0.85), band_shape) * alpha;

    OUT.data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    OUT.data2 = float4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

    float4 out_col = float4(col, alpha);
#if defined(HAS_EMISSIVE)
    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data3 = out_col;
#else
    OUT.data0 = out_col;
#endif

    return OUT;
}
