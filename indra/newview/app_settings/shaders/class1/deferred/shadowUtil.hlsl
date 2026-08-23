/**
 * @file class1/deferred/shadowUtil.hlsl
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

#if defined(SUN_SHADOW)
Texture2D shadowMap0 : register(t10);
SamplerComparisonState shadowMap0Sampler : register(s10);
Texture2D shadowMap1 : register(t11);
SamplerComparisonState shadowMap1Sampler : register(s11);
Texture2D shadowMap2 : register(t12);
SamplerComparisonState shadowMap2Sampler : register(s12);
Texture2D shadowMap3 : register(t13);
SamplerComparisonState shadowMap3Sampler : register(s13);
#endif

#if defined(SPOT_SHADOW)
Texture2D shadowMap4 : register(t14);
SamplerComparisonState shadowMap4Sampler : register(s14);
Texture2D shadowMap5 : register(t15);
SamplerComparisonState shadowMap5Sampler : register(s15);
#endif

// sun_dir/moon_dir are also declared (and actually used) by
// softenLightF.hlsl - genuinely dual-use, same reasoning as color/size -
// include-guarded rather than left bare.
#ifndef LL_SUN_MOON_DIR_DECLARED
#define LL_SUN_MOON_DIR_DECLARED
uniform float3 sun_dir;
uniform float3 moon_dir;
#endif
uniform float2 shadow_res;
uniform float2 proj_shadow_res;
uniform float4x4 shadow_matrix[6];
uniform float4 shadow_clip;
uniform float shadow_bias;
uniform float shadow_offset;
uniform float spot_shadow_bias;
uniform float spot_shadow_offset;

// inv_proj/screen_res are also declared by deferredUtil.hlsl/several
// light shaders (all grouped under the same guard) - reuse it here.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif

// sun_up_factor is also declared by atmosphericsFuncs.hlsl, guarded there -
// reuse the same guard here (sun_dir/moon_dir just above are NOT declared
// by atmosphericsFuncs.hlsl - confirmed via grep, different names there -
// so only sun_up_factor needs this).
#ifndef LL_SUN_UP_FACTOR_DECLARED
#define LL_SUN_UP_FACTOR_DECLARED
uniform int sun_up_factor;
#endif

float pcfShadow(Texture2D shadowMap, SamplerComparisonState shadowSampler, float3 norm, float4 stc, float bias_mul, float2 pos_screen, float3 light_dir)
{
#if defined(SUN_SHADOW)
    float offset = shadow_bias * bias_mul;
    stc.xyz /= stc.w;
    // S24 (2026-08-11, task #158): GL-vs-D3D11 texture-origin flip, same
    // class of fix already applied throughout this session (G-buffer/depth/
    // lightMap reads, aoUtil.hlsl's SSAO) - stc.xy is computed via the
    // shared/ported trans*proj*view*inv_view chain (generateSunShadow(),
    // pipeline.cpp), which assumes GL's texture-row convention. stc is only
    // ever used for shadow-map sampling in this function (never position
    // math), so flipping it once here (rather than per-tap at each of the 5
    // SampleCmpLevelZero() calls below) is safe and equivalent.
    stc.y = 1.0 - stc.y;
    stc.z += offset * 2.0;
    stc.x = floor(stc.x * shadow_res.x + frac(pos_screen.y * shadow_res.y)) / shadow_res.x;
    float cs = shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy, stc.z);
    float shadow_val = cs * 4.0;
    shadow_val += shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy + float2(1.5 / shadow_res.x, 0.5 / shadow_res.y), stc.z);
    shadow_val += shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy + float2(0.5 / shadow_res.x, -1.5 / shadow_res.y), stc.z);
    shadow_val += shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy + float2(-1.5 / shadow_res.x, -0.5 / shadow_res.y), stc.z);
    shadow_val += shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy + float2(-0.5 / shadow_res.x, 1.5 / shadow_res.y), stc.z);
    return clamp(shadow_val * 0.125, 0.0, 1.0);
#else
    return 1.0;
#endif
}

float pcfSpotShadow(Texture2D shadowMap, SamplerComparisonState shadowSampler, float4 stc, float bias_scale, float2 pos_screen)
{
#if defined(SPOT_SHADOW)
    stc.xyz /= stc.w;
    // S24 (2026-08-11, task #158): same GL-vs-D3D11 texture-origin flip as
    // pcfShadow() above - see its comment.
    stc.y = 1.0 - stc.y;
    stc.z += spot_shadow_bias * bias_scale;
    stc.x = floor(proj_shadow_res.x * stc.x + frac(pos_screen.y * 0.666666666)) / proj_shadow_res.x;

    float cs = shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy, stc.z);
    float shadow_val = cs;

    float2 off = 1.0 / proj_shadow_res;
    off.y *= 1.5;

    shadow_val += shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy + float3(off.x * 2.0, off.y, 0.0), stc.z);
    shadow_val += shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy + float3(off.x, -off.y, 0.0), stc.z);
    shadow_val += shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy + float3(-off.x, off.y, 0.0), stc.z);
    shadow_val += shadowMap.SampleCmpLevelZero(shadowSampler, stc.xy + float3(-off.x * 2.0, -off.y, 0.0), stc.z);
    return shadow_val * 0.2;
#else
    return 1.0;
#endif
}

float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen)
{
#if defined(SUN_SHADOW)
    float shadow = 0.0f;
    float3 light_dir = normalize((sun_up_factor == 1) ? sun_dir : moon_dir);

    float dp_directional_light = max(0.0, dot(norm.xyz, light_dir));
    dp_directional_light = clamp(dp_directional_light, 0.0, 1.0);

    float3 shadow_pos = pos.xyz;
    float3 offset = light_dir.xyz * (1.0 - dp_directional_light);
    shadow_pos += offset * shadow_offset * 2.0;

    float4 spos = float4(shadow_pos.xyz, 1.0);

    if (spos.z > -shadow_clip.w)
    {
        float4 lpos;
        float4 near_split = shadow_clip * -0.75;
        float4 far_split = shadow_clip * -1.25;
        float4 transition_domain = near_split - far_split;
        float weight = 0.0;

        if (spos.z < near_split.z)
        {
            lpos = mul(shadow_matrix[3], spos);
            float w = 1.0;
            w -= max(spos.z - far_split.z, 0.0) / transition_domain.z;
            float contrib = pcfShadow(shadowMap3, shadowMap3Sampler, norm, lpos, 1.0, pos_screen, light_dir) * w;
            shadow += contrib;
            weight += w;
            shadow += max((pos.z + shadow_clip.z) / (shadow_clip.z - shadow_clip.w) * 2.0 - 1.0, 0.0);
        }

        if (spos.z < near_split.y && spos.z > far_split.z)
        {
            lpos = mul(shadow_matrix[2], spos);
            float w = 1.0;
            w -= max(spos.z - far_split.y, 0.0) / transition_domain.y;
            w -= max(near_split.z - spos.z, 0.0) / transition_domain.z;
            float contrib = pcfShadow(shadowMap2, shadowMap2Sampler, norm, lpos, 1.0, pos_screen, light_dir) * w;
            shadow += contrib;
            weight += w;
        }

        if (spos.z < near_split.x && spos.z > far_split.y)
        {
            lpos = mul(shadow_matrix[1], spos);
            float w = 1.0;
            w -= max(spos.z - far_split.x, 0.0) / transition_domain.x;
            w -= max(near_split.y - spos.z, 0.0) / transition_domain.y;
            float contrib = pcfShadow(shadowMap1, shadowMap1Sampler, norm, lpos, 1.0, pos_screen, light_dir) * w;
            shadow += contrib;
            weight += w;
        }

        if (spos.z > far_split.x)
        {
            lpos = mul(shadow_matrix[0], spos);
            float w = 1.0;
            w -= max(near_split.x - spos.z, 0.0) / transition_domain.x;
            float contrib = pcfShadow(shadowMap0, shadowMap0Sampler, norm, lpos, 1.0, pos_screen, light_dir) * w;
            shadow += contrib;
            weight += w;
        }

        shadow /= weight;
    }
    else
    {
        return 1.0f;
    }
    return shadow;
#else
    return 1.0;
#endif
}

float sampleSpotShadow(float3 pos, float3 norm, int index, float2 pos_screen)
{
#if defined(SPOT_SHADOW)
    // S24 (2026-08-19, task #232, task #227 audit finding): this whole
    // function was a wrong re-derivation, not a port - it copied
    // sampleDirectionalShadow()'s light_dir/dp_directional_light-based
    // offset instead of shadowUtil.glsl's real (and much simpler) normal
    // offset, hardcoded bias_scale to 1.0 instead of 0.8, dropped the
    // distance-based falloff weight (w/weight) and the additive
    // near-clip term entirely, added an `if (lpos.z > 0.0)` gate GLSL
    // doesn't have, and passed the real screen-space pos_screen into
    // pcfSpotShadow()'s dither-snap instead of GLSL's spos.xy. Re-ported
    // faithfully from shadowUtil.glsl's real sampleSpotShadow() below.
    float shadow = 0.0f;
    pos += norm * spot_shadow_offset;

    float4 spos = float4(pos, 1.0);
    if (spos.z > -shadow_clip.w)
    {
        float4 lpos;

        float4 near_split = shadow_clip * -0.75;
        float4 far_split = shadow_clip * -1.25;
        float4 transition_domain = near_split - far_split;
        float weight = 0.0;

        {
            float w = 1.0;
            w -= max(spos.z - far_split.z, 0.0) / transition_domain.z;

            if (index == 0)
            {
                lpos = mul(shadow_matrix[4], spos);
                shadow += pcfSpotShadow(shadowMap4, shadowMap4Sampler, lpos, 0.8, spos.xy) * w;
            }
            else
            {
                lpos = mul(shadow_matrix[5], spos);
                shadow += pcfSpotShadow(shadowMap5, shadowMap5Sampler, lpos, 0.8, spos.xy) * w;
            }
            weight += w;
            shadow += max((pos.z + shadow_clip.z) / (shadow_clip.z - shadow_clip.w) * 2.0 - 1.0, 0.0);
        }

        shadow /= weight;
    }
    else
    {
        shadow = 1.0f;
    }
    return shadow;
#else
    return 1.0;
#endif
}
