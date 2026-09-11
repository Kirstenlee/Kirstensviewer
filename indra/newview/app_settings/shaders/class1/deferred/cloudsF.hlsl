/**
 * @file class1/deferred/cloudsF.hlsl
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

/////////////////////////////////////////////////////////////////////////
// The fragment shader for the sky
/////////////////////////////////////////////////////////////////////////

Texture2D cloud_noise_texture : register(t0);
SamplerState cloud_noise_textureSampler : register(s0);
Texture2D cloud_noise_texture_next : register(t1);
SamplerState cloud_noise_texture_nextSampler : register(s1);
uniform float blend_factor;
uniform float3 cloud_pos_density1;
uniform float3 cloud_pos_density2;
uniform float cloud_scale;
uniform float cloud_variance;

// S24 (2026-08-31, task #303 "2.5D cloud layers"): additional decks are
// just extra draws of this SAME dome/shader with different CLOUD_SCALE/
// CLOUD_POS_DENSITY1/2 uniform overrides (see dxdrawpoolwlsky.cpp's
// renderSkyCloudsDeferred()) - this tint/alpha pair is the only genuinely
// new shader-side addition, giving each deck its own aerial-perspective
// colour cast and opacity so they read as physically distinct layers
// rather than the same cloud pattern redrawn at a different scale.
// Explicitly reset to (1,1,1)/1.0 before the base/unchanged layer's draw
// every frame - GPU shader constants persist across draw calls, so a
// previous layer's values would otherwise leak into the next frame's base
// pass if this shader happened to be reused without an override.
uniform float3 cloud_layer_tint;
uniform float cloud_layer_alpha_mult;

#include "varying/cloudsVarying.hlsli"

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

float4 cloudNoise(float2 uv)
{
   // S24 (2026-08-09, task #164): near the WLSky dome's planar-pole UV
   // singularity (straight up/down - buildStripsBuffer()'s planar UV
   // formula pinches every vertex there toward the same UV regardless of
   // longitude), adjacent screen pixels can map to wildly different UV
   // coordinates. Sample()'s automatic screen-space derivatives explode
   // at that discontinuity, and the resulting mip/LOD selection is a
   // genuine, implementation-defined difference between HLSL and GLSL at
   // singularities like this - confirmed via GL vs DX screenshot
   // comparison (GL shows a smooth gray gradient there, DX shows a sharp
   // aliased "sunburst" of full-opacity noise) plus a live OM blend-state
   // readback and a real-alpha recolor test that both confirmed alpha
   // blending itself is correct on both backends - this is a texture-
   // sampling aliasing artifact at the singularity, not a blend/alpha
   // bug. Computing derivatives explicitly and clamping their magnitude
   // bounds the mip selection at the singularity without changing
   // sampling anywhere else the derivatives are already small.
   float2 dx = clamp(ddx(uv), -0.05, 0.05);
   float2 dy = clamp(ddy(uv), -0.05, 0.05);
   float4 a = cloud_noise_texture.SampleGrad(cloud_noise_textureSampler, uv, dx, dy);
   float4 b = cloud_noise_texture_next.SampleGrad(cloud_noise_texture_nextSampler, uv, dx, dy);
   float4 cloud_noise_sample = lerp(a, b, blend_factor);
   return cloud_noise_sample;
}

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input.
struct PSInput
{
    float4 position : SV_Position;
    CloudsVarying varying;
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // Set variables
    float2 uv1 = IN.varying.vary_texcoord0.xy;
    float2 uv2 = IN.varying.vary_texcoord1.xy;

    float3 cloudColorSun = IN.varying.vary_CloudColorSun;
    float3 cloudColorAmbient = IN.varying.vary_CloudColorAmbient;
    float cloudDensity = IN.varying.vary_CloudDensity;
    float2 uv3 = IN.varying.vary_texcoord2.xy;
    float2 uv4 = IN.varying.vary_texcoord3.xy;

    if (cloud_scale < 0.001)
    {
        discard;
    }

    float2 disturbance  = float2(cloudNoise(uv1 / 8.0f).x, cloudNoise((uv3 + uv1) / 16.0f).x) * cloud_variance * (1.0f - cloud_scale * 0.25f);
    float2 disturbance2 = float2(cloudNoise((uv1 + uv3) / 4.0f).x, cloudNoise((uv4 + uv2) / 8.0f).x) * cloud_variance * (1.0f - cloud_scale * 0.25f);

    // Offset texture coords
    uv1 += cloud_pos_density1.xy + (disturbance * 0.2);    //large texture, visible density
    uv2 += cloud_pos_density1.xy;   //large texture, self shadow
    uv3 += cloud_pos_density2.xy;   //small texture, visible density
    uv4 += cloud_pos_density2.xy;   //small texture, self shadow

    float density_variance = min(1.0, (disturbance.x* 2.0 + disturbance.y* 2.0 + disturbance2.x + disturbance2.y) * 4.0);

    cloudDensity *= 1.0 - (density_variance * density_variance);

    // Compute alpha1, the main cloud opacity

    float alpha1 = (cloudNoise(uv1).x - 0.5) + (cloudNoise(uv3).x - 0.5) * cloud_pos_density2.z;
    alpha1 = min(max(alpha1 + cloudDensity, 0.) * 10 * cloud_pos_density1.z, 1.);

    // And smooth
    alpha1 = 1. - alpha1 * alpha1;
    alpha1 = 1. - alpha1 * alpha1;

    alpha1 *= IN.varying.altitude_blend_factor;
    alpha1 = clamp(alpha1, 0.0, 1.0);

    // Compute alpha2, for self shadowing effect
    // (1 - alpha2) will later be used as percentage of incoming sunlight
    float alpha2 = (cloudNoise(uv2).x - 0.5);
    alpha2 = min(max(alpha2 + cloudDensity, 0.) * 2.5 * cloud_pos_density1.z, 1.);

    // And smooth
    alpha2 = 1. - alpha2;
    alpha2 = 1. - alpha2 * alpha2;

    // Combine
    float3 color;
    color = (cloudColorSun*(1.-alpha2) + cloudColorAmbient);
    color.rgb = clamp(color.rgb, float3(0, 0, 0), float3(1, 1, 1));
    color.rgb *= 2.0;

    // S24 (task #303): per-layer aerial-perspective tint + opacity - see
    // the uniform declarations above.
    color.rgb *= cloud_layer_tint;
    alpha1 *= cloud_layer_alpha_mult;

    // S24 (2026-09-05, task #312): alpha1 was already clamped to [0,1]
    // above (line ~143) BEFORE this cloud_layer_alpha_mult multiply - but
    // nothing re-clamped it after. cloud_layer_alpha_mult is
    // RenderCloudLayerOpacity (KVTweaks slider, documented/allowed up to
    // 2.0) times 0.4 (cirrus) or 0.75 (cumulus), so at higher slider
    // settings this can genuinely exceed 1.0. Since the PRE-multiplier
    // alpha1 is itself a noise function - only close to 1.0 at sparse
    // density peaks, mostly well below it elsewhere - only those isolated
    // peaks were ever pushed over 1.0, not the whole cloud layer: an
    // alpha >1 reaching the blend equation is undefined territory (some
    // hardware/format combinations can turn InvSrcAlpha=1-alpha negative,
    // subtracting rather than blending). This is the confirmed root cause
    // of "stars/sky render as black dots under clouds" (user bisected it
    // directly to RenderCloudLayerOpacity above ~1.2, independent of
    // anything else touched this session) - re-clamp so this shader never
    // outputs an out-of-range alpha regardless of how high the slider goes.
    alpha1 = saturate(alpha1);

    /// Gamma correct for WL (soft clip effect).

    OUT.data1 = float4(0.0, 0.0, 0.0, 0.0);
    OUT.data2 = float4(0, 0, 0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data3 = float4(color.rgb, alpha1);
#else
    OUT.data0 = float4(color.rgb, alpha1);
#endif

    return OUT;
}
