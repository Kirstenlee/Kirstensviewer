/**
 * @file class3/deferred/multiPointLightF.hlsl
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

// See pointLightF.hlsl's comment - t0-t3/s0-s3 reserved by deferredUtil.hlsl,
// t4-t6/s4-s6 by gbufferUtil.hlsl, t10-t15/s10-s15 by shadowUtil.hlsl.
Texture2D lightFunc : register(t7);
SamplerState lightFuncSampler : register(s7);

uniform float3 env_mat[3];
uniform float sun_wash;
uniform int light_count;
uniform float4 light[LIGHT_COUNT];
uniform float4 light_col[LIGHT_COUNT];

uniform float far_z;

// screen_res/inv_proj are also declared by deferredUtil.hlsl (grouped
// together there under one guard) - reuse it here, same reasoning as
// pointLightF.hlsl's fix. Kept adjacent under ONE #ifndef block (unlike
// an earlier version of this fix, which split them into two separate
// blocks checking the same macro - the first block's #define caused the
// second to see the guard already set and wrongly skip its own
// declaration, leaving inv_proj undeclared entirely. Same documented
// "guard grouping" lesson as earlier this session, re-learned the hard
// way here.)
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float2 screen_res;
uniform float4x4 inv_proj;
#endif

// See pointLightF.hlsl's comment - classic_mode is also declared by
// deferredUtil.hlsl/atmosphericsFuncs.hlsl, include-guarded.
#ifndef LL_CLASSIC_MODE_DECLARED
#define LL_CLASSIC_MODE_DECLARED
uniform int classic_mode;
#endif

float calcLegacyDistanceAttenuation(float distance, float falloff_val);
void calcHalfVectors(float3 lv, float3 n, float3 v, out float3 h, out float3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);
float4 getPosition(float2 pos_screen);
float2 getScreenCoord(float4 clip);
float3 srgb_to_linear(float3 c);
void pbrPunctual(float3 diffuseColor, float3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    float3 n,
                    float3 v,
                    float3 l,
                    out float nl,
                    out float3 diff,
                    out float3 spec);

// See pointLightF.hlsl's comment - GBufferInfo is defined for real in
// gbufferUtil.hlsl, attached after this file's own text, but this file's
// own forward declaration below needs the type visible already.
#ifndef LL_GBUFFERINFO_DECLARED
#define LL_GBUFFERINFO_DECLARED
struct GBufferInfo
{
    float4 albedo;
    float3 normal;
    float4 specular;
    float envIntensity;
    float gbufferFlag;
    float4 emissive;
};
#endif

GBufferInfo getGBuffer(float2 screenpos);

struct PSInput
{
    // S24: SV_Position semantic required here, or every subsequent VS/PS interpolant register shifts (see uiF.hlsl).
    float4 position : SV_Position;

    float4 vary_fragcoord : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    float3 final_color = float3(0, 0, 0);
    float2 tc = getScreenCoord(IN.vary_fragcoord);
    float3 pos = getPosition(tc).xyz;
    if (pos.z < far_z) discard;

    GBufferInfo gb = getGBuffer(tc);
    float3 n = gb.normal;
    float4 spec = gb.specular;
    float3 diffuse = gb.albedo.rgb;
    float3 v = -normalize(pos);

    if (GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_PBR))
    {
        float3 orm = spec.rgb;
        float perceptualRoughness = orm.g;
        float metallic = orm.b;
        float3 f0 = 0.04;
        float3 baseColor = diffuse.rgb;
        // S24: *(1.0-f0) term required to match GLSL - see spotLightF.hlsl's copy.
        float3 diffuseColor = baseColor.rgb * (float3(1.0, 1.0, 1.0) - f0);
        diffuseColor *= 1.0 - metallic;
        float3 specularColor = lerp(f0, baseColor.rgb, metallic);

        // S24: must loop the runtime light_count uniform, not the compile-time LIGHT_COUNT
        // permutation constant (matches GL's multiLightF.glsl `i < light_count`) - looping
        // LIGHT_COUNT wastes iterations and risks phantom lights if an unused slot is ever
        // left non-zero.
        int clamped_light_count = min(light_count, LIGHT_COUNT);
        for (int light_idx = 0; light_idx < clamped_light_count; ++light_idx)
        {
            float3 lightColor = light_col[light_idx].rgb;
            float falloff_val = light_col[light_idx].a;
            float lightSize = light[light_idx].w;
            float3 lv = light[light_idx].xyz - pos;
            float lightDist = length(lv);
            float dist = lightDist / lightSize;
            if (dist <= 1.0)
            {
                lv /= lightDist;
                float dist_atten = calcLegacyDistanceAttenuation(dist, falloff_val);
                float3 intensity = dist_atten * lightColor * 3.25;
                float nl2 = 0; float3 diffPunc, specPunc;
                pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, n.xyz, v, lv, nl2, diffPunc, specPunc);
                final_color += intensity * clamp(nl2 * (diffPunc + specPunc), 0.0, 10.0);
            }
        }
    }
    else
    {
        diffuse = srgb_to_linear(diffuse);
        spec.rgb = srgb_to_linear(spec.rgb);
        // S24: same LIGHT_COUNT -> light_count correction as the PBR branch above.
        int clamped_light_count_legacy = min(light_count, LIGHT_COUNT);
        for (int i = 0; i < clamped_light_count_legacy; ++i)
        {
            float3 lv = light[i].xyz - pos;
            float dist = length(lv) / light[i].w;
            if (dist <= 1.0)
            {
                float nl2 = dot(n, lv / length(lv));
                // S24: a hard `nl2 > 0.0` cutoff pops on/off between frames under continuous
                // camera micro-motion (mouse-look, idle sway) - a temporal-aliasing artifact
                // from resampling a step function every frame, not a state/geometry bug.
                // smoothstep across a small epsilon band around nl2=0 removes the popping
                // since a continuous function's value can only change proportionally to a
                // proportionally small angle change. Deliberate DX-only improvement - GL's
                // pipeline.cpp keeps the same unsmoothed cutoff pointLightF.hlsl also has.
                const float NL_SMOOTH_EPS = 0.05; // ~87-93 degree grazing band
                float nl_atten = smoothstep(-NL_SMOOTH_EPS, NL_SMOOTH_EPS, nl2);
                if (nl_atten > 0.0)
                {
                    float3 h, l; float nh, nv, vh, _lightDist;
                    calcHalfVectors(lv, n, v, h, l, nh, nl2, nv, vh, _lightDist);
                    float fa = light_col[i].a;
                    float dist_atten = calcLegacyDistanceAttenuation(dist, fa);
                    float lit = nl2 * dist_atten;
                    float3 col = light_col[i].rgb * lit * diffuse;
                    if (spec.a > 0.0)
                    {
                        lit = min(nl2 * 6.0, 1.0) * dist_atten;
                        float fres = pow(abs(1 - vh), 5) * 0.4 + 0.5;
                        float gt = max(0, min(2 * nh * nv / vh, 2 * nh * nl2 / vh));
                        if (nh > 0.0)
                        {
                            // S24: SampleLevel(...,0) instead of Sample() - this loop's trip
                            // count depends on the runtime light_count uniform, so FXC can't
                            // prove uniform derivatives across a quad and forcibly unrolls
                            // the loop to make Sample()'s implicit LOD provable (X3570).
                            // Equivalent here since mDXLightFunc has only mip 0 (MipLevels=1).
                            float scol = fres * lightFunc.SampleLevel(lightFuncSampler, float2(nh, spec.a), 0).r * gt / (nh * nl2);
                            col += lit * scol * light_col[i].rgb * spec.rgb;
                        }
                    }
                    // nl2 itself is already clamped to >=1e-6 by
                    // calcHalfVectors() above, so `col` alone never goes
                    // negative here - nl_atten is what actually implements
                    // the smooth fade to zero as nl2 crosses the epsilon
                    // band from the shading side (the pre-clamp nl2 value
                    // used for the ramp is the one computed above, before
                    // calcHalfVectors overwrites it).
                    final_color += col * nl_atten;
                }
            }
        }
    }
    float final_scale = (classic_mode > 0) ? 0.9 : 1.0;
    return float4(max(final_color * final_scale, 0.0), 0.0);
}
