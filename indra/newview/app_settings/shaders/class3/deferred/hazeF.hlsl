/**
 * @file class3/deferred/hazeF.hlsl
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

// Inputs
// sun_dir/moon_dir/sun_up_factor are also declared (and used) by
// shadowUtil.hlsl/atmosphericsFuncs.hlsl - genuinely used below too
// (light_dir selection) - reuse the existing guards.
#ifndef LL_SUN_MOON_DIR_DECLARED
#define LL_SUN_MOON_DIR_DECLARED
uniform float3 sun_dir;
uniform float3 moon_dir;
#endif
#ifndef LL_SUN_UP_FACTOR_DECLARED
#define LL_SUN_UP_FACTOR_DECLARED
uniform int sun_up_factor;
#endif

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

float4 getNorm(float2 pos_screen);
float4 getPositionWithDepth(float2 pos_screen, float depth);
void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);

float getDepth(float2 pos_screen);

float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

// waterPlane is also declared (and used) by deferredUtil.hlsl/
// waterFogF.hlsl (already guarded there, independent attach conditions) -
// this copy is genuinely used below too (do_atmospherics check) - reuse
// the existing guard rather than deleting.
#ifndef LL_WATERPLANE_DECLARED
#define LL_WATERPLANE_DECLARED
uniform float4 waterPlane;
#endif

// S24 (2026-08-11, task #156 hotfix round 5): also declared (guarded) by
// reflectionProbeF.hlsl/softenLightF.hlsl, both always co-attached
// whenever this file is - unguarded copy caused an X3003 redefinition in
// "Haze Shader", same collision class as depthMapSampler/inv_proj/
// screen_res earlier in this same hotfix chain.
#ifndef LL_CUBE_SNAPSHOT_DECLARED
#define LL_CUBE_SNAPSHOT_DECLARED
uniform int cube_snapshot;
#endif

// sky_hdr_scale is also declared (and used) by atmosphericsF.hlsl -
// genuinely used below too (additive color scale) - reuse the existing
// guard.
#ifndef LL_SKY_HDR_SCALE_DECLARED
#define LL_SKY_HDR_SCALE_DECLARED
uniform float sky_hdr_scale;
#endif

float4 main(PSInput IN) : SV_Target
{
    float2  tc           = IN.vary_fragcoord.xy;
    float depth        = getDepth(tc.xy);
    float4  pos          = getPositionWithDepth(tc, depth);
    float4  norm         = getNorm(tc);
    float3  light_dir   = (sun_up_factor == 1) ? sun_dir : moon_dir;

    float3  color = float3(0, 0, 0);
    float bloom = 0.0;

    float3 sunlit;
    float3 amblit;
    float3 additive;
    float3 atten;

    calcAtmosphericVarsLinear(pos.xyz, norm.xyz, light_dir, sunlit, amblit, additive, atten);

    // mask off atmospherics below water (when camera is under water)
    bool do_atmospherics = false;

    if (dot(float3(0, 0, 0), waterPlane.xyz) + waterPlane.w > 0.0 ||
        dot(pos.xyz, waterPlane.xyz) + waterPlane.w > 0.0)
    {
        do_atmospherics = true;
    }

    float3  irradiance = float3(0, 0, 0);
    float3  radiance  = float3(0, 0, 0);

    // S24 (reversed-Z conversion): far is now 0.0, was 1.0 - see
    // kGLtoDXDepthRemap's comment (llrender.cpp).
    if (depth <= 0.0)
    {
        //should only be true of sky, clouds, sun/moon, and stars
        discard;
    }

   float alpha = 0.0;

    if (do_atmospherics)
    {
        alpha = atten.r;
        color = srgb_to_linear(additive*2.0);
        color *= sky_hdr_scale;
    }
    else
    {
        color = float3(0, 0, 0);
        alpha = 1.0;
    }

    return max(float4(color.rgb, alpha), float4(0, 0, 0, 0)); //output linear since local lights will be added to this shader's results
}
