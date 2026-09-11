/**
 * @file class1/deferred/skyV.hlsl
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

uniform float4x4 modelview_projection_matrix;

// SKY ////////////////////////////////////////////////////////////////////////
// The vertex shader for creating the atmospheric sky
///////////////////////////////////////////////////////////////////////////////

// Inputs
uniform float3 camPosLocal;

// lightnorm/sunlight_color/moonlight_color are also declared (and used) by
// atmosphericsFuncs.hlsl, always attached alongside this file's vertex
// stage whenever calculatesAtmospherics is set (this shader's own case) -
// genuinely dual-use, include-guarded.
#ifndef LL_LIGHTNORM_DECLARED
#define LL_LIGHTNORM_DECLARED
uniform float3  lightnorm;
#endif
#ifndef LL_SUNLIGHT_MOONLIGHT_COLOR_DECLARED
#define LL_SUNLIGHT_MOONLIGHT_COLOR_DECLARED
uniform float3  sunlight_color;
uniform float3  moonlight_color;
#endif
// sun_up_factor is also declared by atmosphericsFuncs.hlsl - reuse the
// existing guard.
#ifndef LL_SUN_UP_FACTOR_DECLARED
#define LL_SUN_UP_FACTOR_DECLARED
uniform int   sun_up_factor;
#endif
// ambient_color..density_multiplier are also declared (and used) by
// atmosphericsFuncs.hlsl - reuse the existing guard.
#ifndef LL_ATMOS_HAZE_PARAMS_DECLARED
#define LL_ATMOS_HAZE_PARAMS_DECLARED
uniform float3  ambient_color;
uniform float3  blue_horizon;
uniform float3  blue_density;
uniform float haze_horizon;
uniform float haze_density;

uniform float cloud_shadow;
uniform float density_multiplier;
#endif
// distance_multiplier is also declared by atmosphericsFuncs.hlsl, but kept
// as its own guard there (not shared with cloudsV.hlsl's matching group) -
// reuse that same guard here.
#ifndef LL_DISTANCE_MULTIPLIER_DECLARED
#define LL_DISTANCE_MULTIPLIER_DECLARED
uniform float distance_multiplier;
#endif
// max_y/glow/sun_moon_glow_factor are also declared (and used) by
// atmosphericsFuncs.hlsl - reuse the existing guard.
#ifndef LL_ATMOS_GLOW_PARAMS_DECLARED
#define LL_ATMOS_GLOW_PARAMS_DECLARED
uniform float max_y;

uniform float3  glow;
uniform float sun_moon_glow_factor;
#endif

// S24 (2026-09-07): guarded, was bare - atmosphericsFuncs.hlsl now also
// declares this (needed there for ambientLighting()'s probe-capture check,
// task #266) and is co-attached here via calculatesAtmospherics/
// hasAtmospherics, causing a real X3003 redefinition compile failure
// ("Environment Map Program") without this guard. Same include-guard
// pattern already established throughout this codebase for dual-declared
// uniforms (see reflectionProbeF.hlsl/softenLightF.hlsl's own identical
// guard).
#ifndef LL_CUBE_SNAPSHOT_DECLARED
#define LL_CUBE_SNAPSHOT_DECLARED
uniform int cube_snapshot;
#endif

#include "varying/skyVarying.hlsli"

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    SkyVarying varying;
};

// NOTE: Keep these in sync!
//       indra\newview\app_settings\shaders\class1\deferred\skyV.glsl
//       indra\newview\app_settings\shaders\class1\deferred\cloudsV.glsl
//       indra\newview\lllegacyatmospherics.cpp
VSOutput main(VSInput IN)
{
    VSOutput OUT;

    // World / view / projection
    float4 pos = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));

    OUT.position = pos;

    // Get relative position
    float3 rel_pos = IN.position.xyz - camPosLocal.xyz + float3(0, 50, 0);

#ifdef HAS_HDRI
    OUT.varying.vary_rel_pos = rel_pos;
    OUT.varying.vary_position = pos;
#endif

    // Adj position vector to clamp altitude
    if (rel_pos.y > 0.)
    {
        rel_pos *= (max_y / rel_pos.y);
    }
    if (rel_pos.y < 0.)
    {
        rel_pos *= (-32000. / rel_pos.y);
    }

    // Normalized
    float3  rel_pos_norm = normalize(rel_pos);
    float rel_pos_len  = length(rel_pos);

    // Grab this value and pass to frag shader for rainbows
    float rel_pos_lightnorm_dot = dot(rel_pos_norm, lightnorm.xyz);
    OUT.varying.vary_LightNormPosDot = rel_pos_lightnorm_dot;

    // Initialize temp variables
    float3 sunlight = (sun_up_factor == 1) ? sunlight_color : moonlight_color * 0.7; //magic 0.7 to match legacy color

    // Sunlight attenuation effect (hue and brightness) due to atmosphere
    // this is used later for sunlight modulation at various altitudes
    float3 light_atten = (blue_density + float3(haze_density * 0.25, haze_density * 0.25, haze_density * 0.25)) * (density_multiplier * max_y);

    // Calculate relative weights
    float3 combined_haze = max(abs(blue_density) + float3(abs(haze_density), abs(haze_density), abs(haze_density)), float3(1e-6, 1e-6, 1e-6));
    float3 blue_weight   = blue_density / combined_haze;
    float3 haze_weight   = haze_density / combined_haze;

    // Compute sunlight from rel_pos & lightnorm (for long rays like sky)
    float off_axis = 1.0 / max(1e-6, max(0., rel_pos_norm.y) + lightnorm.y);
    sunlight *= exp(-light_atten * off_axis);

    // Distance
    float density_dist = rel_pos_len * density_multiplier;

    // Transparency (-> combined_haze)
    // ATI Bugfix -- can't store combined_haze*density_dist in a variable because the ati
    // compiler gets confused.
    combined_haze = exp(-combined_haze * density_dist);

    // Compute haze glow
    float haze_glow = 1.0 - rel_pos_lightnorm_dot;
    // haze_glow is 0 at the sun and increases away from sun
    haze_glow = max(haze_glow, .001);
    // Set a minimum "angle" (smaller glow.y allows tighter, brighter hotspot)
    haze_glow *= glow.x;
    // Higher glow.x gives dimmer glow (because next step is 1 / "angle")
    haze_glow = pow(abs(haze_glow), glow.z);
    // glow.z should be negative, so we're doing a sort of (1 / "angle") function

    // Add "minimum anti-solar illumination"
    // For sun, add to glow.  For moon, remove glow entirely. SL-13768
    haze_glow = (sun_moon_glow_factor < 1.0) ? 0.0 : (sun_moon_glow_factor * (haze_glow + 0.25));

    // Haze color above cloud
    float3 color = (blue_horizon * blue_weight * (sunlight + ambient_color)
               + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient_color));

    // Final atmosphere additive
    color *= (1. - combined_haze);

    // Increase ambient when there are more clouds
    float3 ambient = ambient_color + max(float3(0, 0, 0), (1. - ambient_color)) * cloud_shadow * 0.5;

    // Dim sunlight by cloud shadow percentage
    sunlight *= max(0.0, (1. - cloud_shadow));

    // Haze color below cloud
    float3 add_below_cloud = (blue_horizon * blue_weight * (sunlight + ambient)
                         + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient));

    // Attenuate cloud color by atmosphere
    combined_haze = sqrt(combined_haze);  // less atmos opacity (more transparency) below clouds

    // At horizon, blend high altitude sky color towards the darker color below the clouds
    color += (add_below_cloud - color) * (1. - sqrt(combined_haze));

    // Haze color above cloud
    OUT.varying.vary_HazeColor = color;

    return OUT;
}
