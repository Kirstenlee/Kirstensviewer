/**
 * @file class1/deferred/cloudsV.hlsl
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

//////////////////////////////////////////////////////////////////////////
// The vertex shader for creating the atmospheric sky
///////////////////////////////////////////////////////////////////////////////

// Inputs
uniform float3 camPosLocal;

// lightnorm/sunlight_color/moonlight_color/sun_up_factor and the whole
// atmospherics parameter block below are also declared (and used) by
// atmosphericsFuncs.hlsl, always attached alongside this file's vertex
// stage whenever calculatesAtmospherics is set (this shader's own case) -
// genuinely dual-use, include-guarded. Note this file does NOT declare
// distance_multiplier (unlike skyV.hlsl, which does) - that guard is
// deliberately not touched here, so atmosphericsFuncs.hlsl's own copy of
// it still declares normally when attached alongside this file.
#ifndef LL_LIGHTNORM_DECLARED
#define LL_LIGHTNORM_DECLARED
uniform float3 lightnorm;
#endif
#ifndef LL_SUNLIGHT_MOONLIGHT_COLOR_DECLARED
#define LL_SUNLIGHT_MOONLIGHT_COLOR_DECLARED
uniform float3 sunlight_color;
uniform float3 moonlight_color;
#endif
#ifndef LL_SUN_UP_FACTOR_DECLARED
#define LL_SUN_UP_FACTOR_DECLARED
uniform int sun_up_factor;
#endif
#ifndef LL_ATMOS_HAZE_PARAMS_DECLARED
#define LL_ATMOS_HAZE_PARAMS_DECLARED
uniform float3 ambient_color;
uniform float3 blue_horizon;
uniform float3 blue_density;
uniform float haze_horizon;
uniform float haze_density;

uniform float cloud_shadow;
uniform float density_multiplier;
#endif

#ifndef LL_ATMOS_GLOW_PARAMS_DECLARED
#define LL_ATMOS_GLOW_PARAMS_DECLARED
uniform float max_y;

uniform float3 glow;
uniform float sun_moon_glow_factor;
#endif

uniform float3 cloud_color;

uniform float cloud_scale;

#include "varying/cloudsVarying.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    CloudsVarying varying;
};

// NOTE: Keep these in sync!
//       indra\newview\app_settings\shaders\class1\deferred\skyV.glsl
//       indra\newview\app_settings\shaders\class1\deferred\cloudsV.glsl
//       indra\newview\app-settings\shaders\class2\windlight\cloudsV.glsl
//       indra\newview\lllegacyatmospherics.cpp
//       indra\newview\llsettingsvo.cpp
VSOutput main(VSInput IN)
{
    VSOutput OUT;

    // World / view / projection
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));

    // Texture coords
    // SL-13084 EEP added support for custom cloud textures -- flip them horizontally to match the preview of Clouds > Cloud Scroll
    OUT.varying.vary_texcoord0 = float2(-IN.texcoord0.x, IN.texcoord0.y);  // See: LLSettingsVOSky::applySpecial

    OUT.varying.vary_texcoord0.xy -= 0.5;
    OUT.varying.vary_texcoord0.xy /= cloud_scale;
    OUT.varying.vary_texcoord0.xy += 0.5;

    OUT.varying.vary_texcoord1 = OUT.varying.vary_texcoord0;
    OUT.varying.vary_texcoord1.x += lightnorm.x * 0.0125;
    OUT.varying.vary_texcoord1.y += lightnorm.z * 0.0125;

    OUT.varying.vary_texcoord2 = OUT.varying.vary_texcoord0 * 16.;
    OUT.varying.vary_texcoord3 = OUT.varying.vary_texcoord1 * 16.;

    // Get relative position
    float3 rel_pos = IN.position.xyz - camPosLocal.xyz + float3(0, 50, 0);

    OUT.varying.altitude_blend_factor = clamp((rel_pos.y + 512.0) / max_y, 0.0, 1.0);

    // Set altitude
    if (rel_pos.y > 0)
    {
        rel_pos *= (max_y / rel_pos.y);
    }
    if (rel_pos.y < 0)
    {
        OUT.varying.altitude_blend_factor = 0; // SL-11589 Fix clouds drooping below horizon
        rel_pos *= (-32000. / rel_pos.y);
    }

    // Can normalize then
    float3  rel_pos_norm = normalize(rel_pos);
    float rel_pos_len  = length(rel_pos);

    // Initialize temp variables
    float3 sunlight = sunlight_color;
    float3 light_atten;

    // Sunlight attenuation effect (hue and brightness) due to atmosphere
    // this is used later for sunlight modulation at various altitudes
    light_atten = (blue_density + float3(haze_density * 0.25, haze_density * 0.25, haze_density * 0.25)) * (density_multiplier * max_y);

    // Calculate relative weights
    float3 combined_haze = abs(blue_density) + float3(abs(haze_density), abs(haze_density), abs(haze_density));
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
    float haze_glow = 1.0 - dot(rel_pos_norm, lightnorm.xyz);
    // haze_glow is 0 at the sun and increases away from sun
    haze_glow = max(haze_glow, .001);
        // Set a minimum "angle" (smaller glow.y allows tighter, brighter hotspot)
    haze_glow *= glow.x;
        // Higher glow.x gives dimmer glow (because next step is 1 / "angle")
    haze_glow = pow(abs(haze_glow), glow.z);
        // glow.z should be negative, so we're doing a sort of (1 / "angle") function

    haze_glow *= sun_moon_glow_factor;

    // Add "minimum anti-solar illumination"
    // For sun, add to glow.  For moon, remove glow entirely. SL-13768
    haze_glow = (sun_moon_glow_factor < 1.0) ? 0.0 : (haze_glow + 0.25);

    // Increase ambient when there are more clouds
    float3 tmpAmbient = ambient_color;
    tmpAmbient += (1. - tmpAmbient) * cloud_shadow * 0.5;

    // Dim sunlight by cloud shadow percentage
    sunlight *= (1. - cloud_shadow);

    // Haze color below cloud
    float3 additiveColorBelowCloud =
        (blue_horizon * blue_weight * (sunlight + tmpAmbient) + (haze_horizon * haze_weight) * (sunlight * haze_glow + tmpAmbient));

    // CLOUDS
    sunlight = sunlight_color;
    off_axis = 1.0 / max(1e-6, lightnorm.y * 2.);
    sunlight *= exp(-light_atten * off_axis);

    // Cloud color out
    OUT.varying.vary_CloudColorSun     = (sunlight * haze_glow) * cloud_color;
    OUT.varying.vary_CloudColorAmbient = tmpAmbient * cloud_color;

    // Attenuate cloud color by atmosphere
    combined_haze = sqrt(combined_haze);  // less atmos opacity (more transparency) below clouds
    OUT.varying.vary_CloudColorSun *= combined_haze;
    OUT.varying.vary_CloudColorAmbient *= combined_haze;
    float3 oHazeColorBelowCloud = additiveColorBelowCloud * (1. - combined_haze);

    // Make a nice cloud density based on the cloud_shadow value that was passed in.
    OUT.varying.vary_CloudDensity = 2. * (cloud_shadow - 0.25);

    // Combine these to minimize register use
    OUT.varying.vary_CloudColorAmbient += oHazeColorBelowCloud;

    // END CLOUDS

    return OUT;
}
