/**
 * @file class3/deferred/softenLightF.hlsl
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

#define FLT_MAX 3.402823466e+38

static const float M_PI = 3.14159265;

#if defined(HAS_SUN_SHADOW) || defined(HAS_SSAO)
Texture2D lightMap : register(t0);
SamplerState lightMapSampler : register(s0);
#endif

Texture2D lightFunc : register(t1);
SamplerState lightFuncSampler : register(s1);

uniform float blur_size;
uniform float blur_fidelity;

#if defined(HAS_SSAO)
uniform float ssao_irradiance_scale;
uniform float ssao_irradiance_max;
#endif

// Inputs
uniform float4 clipPlane;
uniform float3x3 env_mat;
uniform float3x3  ssao_effect_mat;
uniform float3 sun_dir;
uniform float3 moon_dir;
uniform int  sun_up_factor;
uniform int classic_mode;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

uniform float4x4 inv_proj;
uniform float2 screen_res;

float4 getNorm(float2 pos_screen);
float4 getPositionWithDepth(float2 pos_screen, float depth);

void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);
float3  atmosFragLightingLinear(float3 l, float3 additive, float3 atten);
float3  scaleSoftClipFragLinear(float3 l);

// reflection probe interface
void sampleReflectionProbes(inout float3 ambenv, inout float3 glossenv,
    float2 tc, float3 pos, float3 norm, float glossiness, bool transparent, float3 amblit_linear);
void sampleReflectionProbesLegacy(inout float3 ambenv, inout float3 glossenv, inout float3 legacyenv,
        float2 tc, float3 pos, float3 norm, float glossiness, float envIntensity, bool transparent, float3 amblit_linear);
void applyGlossEnv(inout float3 color, float3 glossenv, float4 spec, float3 pos, float3 norm);
void applyLegacyEnv(inout float3 color, float3 legacyenv, float4 spec, float3 pos, float3 norm, float envIntensity);
float getDepth(float2 pos_screen);

float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

uniform float4 waterPlane;

uniform int cube_snapshot;

uniform float sky_hdr_scale;

void calcHalfVectors(float3 lv, float3 n, float3 v, out float3 h, out float3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);
void calcDiffuseSpecular(float3 baseColor, float metallic, inout float3 diffuseColor, inout float3 specularColor);

float3 pbrBaseLight(float3 diffuseColor,
                  float3 specularColor,
                  float metallic,
                  float3 pos,
                  float3 norm,
                  float perceptualRoughness,
                  float3 light_dir,
                  float3 sunlit,
                  float scol,
                  float3 radiance,
                  float3 irradiance,
                  float3 colorEmissive,
                  float ao,
                  float3 additive,
                  float3 atten);

struct GBufferInfo
{
    float4 albedo;
    float3 normal;
    float4 specular;
    float envIntensity;
    float gbufferFlag;
    float4 emissive;
};

GBufferInfo getGBuffer(float2 screenpos);
float3 clampHDRRange(float3 color);

void adjustIrradiance(inout float3 irradiance, float ambocc)
{
    // use sky settings ambient or irradiance map sample, whichever is brighter
    //irradiance = max(amblit_linear, irradiance);

#if defined(HAS_SSAO)
    irradiance = lerp(mul(ssao_effect_mat, min(irradiance.rgb*ssao_irradiance_scale, float3(ssao_irradiance_max, ssao_irradiance_max, ssao_irradiance_max))), irradiance.rgb, ambocc);
#endif
}

float4 main(PSInput IN) : SV_Target
{
    float2  tc           = IN.vary_fragcoord.xy;
    float depth        = getDepth(tc.xy);
    float4  pos          = getPositionWithDepth(tc, depth);

    GBufferInfo gb = getGBuffer(tc);

    float3 colorEmissive = gb.emissive.rgb;
    float envIntensity = gb.envIntensity;
    float3  light_dir   = (sun_up_factor == 1) ? sun_dir : moon_dir;

    float4 baseColor     = gb.albedo;
    float4 spec        = gb.specular; // NOTE: PBR linear Emissive

#if defined(HAS_SUN_SHADOW) || defined(HAS_SSAO)
    float2 scol_ambocc = lightMap.Sample(lightMapSampler, IN.vary_fragcoord.xy).rg;
#endif

#if defined(HAS_SUN_SHADOW)
    float scol       = scol_ambocc.r;
#else
    float scol = 1.0;
#endif
#if defined(HAS_SSAO)
    float ambocc     = scol_ambocc.g;
#else
    float ambocc = 1.0;
#endif

    float3  color = float3(0, 0, 0);
    float bloom = 0.0;

    float3 sunlit;
    float3 amblit;
    float3 additive;
    float3 atten;

    calcAtmosphericVarsLinear(pos.xyz, gb.normal, light_dir, sunlit, amblit, additive, atten);

    if (classic_mode > 0)
        sunlit *= 1.35;

    float3 sunlit_linear = sunlit;
    float3 amblit_linear = amblit;

    float3  radiance  = float3(0, 0, 0);

    float4 frag_color;

    if (GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_PBR))
    {
        float3 orm = spec.rgb;
        float perceptualRoughness = orm.g;
        float metallic = orm.b;
        float ao = orm.r;

        float3  irradiance = amblit_linear;

        // PBR IBL
        float gloss      = 1.0 - perceptualRoughness;

        sampleReflectionProbes(irradiance, radiance, tc, pos.xyz, gb.normal, gloss, false, amblit_linear);

        adjustIrradiance(irradiance, ambocc);

        float3 diffuseColor;
        float3 specularColor;
        calcDiffuseSpecular(baseColor.rgb, metallic, diffuseColor, specularColor);

        float3 v = -normalize(pos.xyz);
        color = pbrBaseLight(diffuseColor, specularColor, metallic, v, gb.normal, perceptualRoughness, light_dir, sunlit_linear, scol, radiance, irradiance, colorEmissive, ao, additive, atten);
    }
    else if (GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_HDRI))
    {
        // actual HDRI sky, just copy color value
        color = colorEmissive.rgb;
    }
    else if (GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_SKIP_ATMOS))
    {
        //should only be true of WL sky, port over base color value and scale for fake HDR
#if defined(HAS_EMISSIVE)
        color = colorEmissive.rgb;
#else
        color = baseColor.rgb;
#endif
        color = srgb_to_linear(color);
        color *= sky_hdr_scale;
    }
    else
    {
        // legacy shaders are still writng sRGB to gbuffer
        baseColor.rgb = srgb_to_linear(baseColor.rgb);

        // For legacy surfaces, baseColor.a indicates fullbright (skip shadows)
        scol = max(scol, baseColor.a);

        spec.rgb = srgb_to_linear(spec.rgb);

        float da          = clamp(dot(gb.normal, light_dir.xyz), 0.0, 1.0);

        float3 irradiance = amblit;
        float3 glossenv = float3(0, 0, 0);
        float3 legacyenv = float3(0, 0, 0);

        sampleReflectionProbesLegacy(irradiance, glossenv, legacyenv, tc, pos.xyz, gb.normal, spec.a, envIntensity, false, amblit_linear);

        adjustIrradiance(irradiance, ambocc);

        // apply lambertian IBL only (see pbrIbl)
        color.rgb = irradiance;

        if (classic_mode > 0)
        {
            da = pow(da,1.2);
            float3 sun_contrib = float3(min(da, scol), min(da, scol), min(da, scol));

            color.rgb = srgb_to_linear(color.rgb * 0.9 + (linear_to_srgb(sun_contrib) * sunlit_linear * 0.7));
            sunlit_linear = srgb_to_linear(sunlit_linear);
        }
        else
        {
            float3 sun_contrib = min(da, scol) * sunlit_linear;
            color.rgb += sun_contrib;
        }

        color.rgb *= baseColor.rgb;

        float3 refnormpersp = reflect(pos.xyz, gb.normal);

        if (spec.a > 0.0)
        {
            float3  lv = light_dir.xyz;
            float3  h, l, v = -normalize(pos.xyz);
            float nh, nl, nv, vh, lightDist;
            float3 n = gb.normal;
            calcHalfVectors(lv, n, v, h, l, nh, nl, nv, vh, lightDist);

            if (nl > 0.0 && nh > 0.0)
            {
                float lit = min(nl*6.0, 1.0);

                float sa = nh;
                float fres = pow(1 - vh, 5) * 0.4+0.5;
                float gtdenom = 2 * nh;
                float gt = max(0,(min(gtdenom * nv / vh, gtdenom * nl / vh)));

                scol *= fres*lightFunc.Sample(lightFuncSampler, float2(nh, spec.a)).r*gt/(nh*nl);
                color.rgb += lit*scol*sunlit_linear.rgb*spec.rgb;
            }

            // add radiance map
            applyGlossEnv(color, glossenv, spec, pos.xyz, gb.normal);

        }

        color.rgb = lerp(color.rgb, baseColor.rgb, baseColor.a);

        if (envIntensity > 0.0)
        {  // add environment map
            applyLegacyEnv(color, legacyenv, spec, pos.xyz, gb.normal, envIntensity);
        }
   }

    //color.r = classic_mode > 0 ? 1.0 : 0.0;
    float final_scale = 1;
    if (classic_mode > 0)
        final_scale = 1.1;

    frag_color.rgb = clampHDRRange(color.rgb * final_scale); //output linear since local lights will be added to this shader's results
    frag_color.a = 0.0;

    return frag_color;
}
