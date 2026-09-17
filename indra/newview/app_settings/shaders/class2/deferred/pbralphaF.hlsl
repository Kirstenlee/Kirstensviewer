/**
 * @file class2/deferred/pbralphaF.hlsl
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

// S24: gHUDPBRAlphaProgram never sets mFeatures.isDeferred, so deferredUtil.hlsl
// (waterClip/pbrBaseLight/pbrIbl/pbrPunctual) is never attached to the HUD
// permutation - split into IS_HUD / non-HUD branches, matching pbropaqueF.hlsl.
#ifndef IS_HUD

// deferred PBR alpha implementation

// t0-t3/s0-s3 are reserved by deferredUtil.hlsl's normalMap/depthMap/
// projectionMap/brdfLut (isDeferred=true for this shader), t4/s4 by
// reflectionProbeF.hlsl's environmentMap, t10-t15/s10-s15 by shadowUtil.hlsl's
// shadowMap0-5 (all attached here too) - moved to t5-t9/s5-s9 to avoid
// X4500 overlapping-register-semantics errors, same pattern as materialF.hlsl.
Texture2D diffuseMap : register(t5);
SamplerState diffuseMapSampler : register(s5);
Texture2D bumpMap : register(t6);
SamplerState bumpMapSampler : register(s6);
Texture2D emissiveMap : register(t7);
SamplerState emissiveMapSampler : register(s7);
Texture2D specularMap : register(t8);
SamplerState specularMapSampler : register(s8);

#if defined(HAS_SUN_SHADOW) || defined(HAS_SSAO)
Texture2D lightMap : register(t9);
SamplerState lightMapSampler : register(s9);
#endif

uniform float metallicFactor;
uniform float roughnessFactor;
uniform float3 emissiveColor;
// sun_up_factor/sun_dir/moon_dir are needed by the real lighting body below -
// re-declared with the same include guards shadowUtil.hlsl's own copies use
// (LL_SUN_UP_FACTOR_DECLARED/LL_SUN_MOON_DIR_DECLARED, see materialF.hlsl's
// identical pattern) so whichever file concatenates first wins, no X3003
// redefinition regardless of whether shadowUtil.hlsl (hasShadows-gated) is
// also attached this build.
#ifndef LL_SUN_UP_FACTOR_DECLARED
#define LL_SUN_UP_FACTOR_DECLARED
uniform int sun_up_factor;
#endif
#ifndef LL_SUN_MOON_DIR_DECLARED
#define LL_SUN_MOON_DIR_DECLARED
uniform float3 sun_dir;
uniform float3 moon_dir;
#endif
// S24: minimum_alpha and its discard check are MASK-mode-only per pbralphaF.glsl
// - must stay gated behind HAS_ALPHA_MASK, or BLEND-mode materials (alphaMode=
// BLEND, e.g. hair) get their soft low-alpha edges hard-discarded.
#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()
#endif
uniform float4 light_position[8];
uniform float3 light_direction[8];
uniform float4 light_attenuation[8];
uniform float3 light_diffuse[8];
uniform float2 light_deferred_attenuation[8];

float3 srgb_to_linear(float3 c);
float3 linear_to_srgb(float3 c);
void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);
float4 applySkyAndWaterFog(float3 pos, float3 additive, float3 atten, float4 color);
void mirrorClip(float3 pos);
void waterClip(float3 pos);
void calcDiffuseSpecular(float3 baseColor, float metallic, inout float3 diffuseColor, inout float3 specularColor);
// S24: body in shadowUtil.hlsl (attached via isDeferred=true).
#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen);
#endif
// S24: tc's divide-by-z approximation (pbralphaV.hlsl's vary_fragcoord has no
// real .w) is fine for the shadow lookup but not for SSR, which needs a true
// perspective-correct screen UV - generateProjectedPosition() (screenSpaceReflUtil.hlsl,
// same helper tapScreenSpaceReflection()'s real callers use, incl. its Y-flip)
// is used for the probe/SSR call instead, leaving tc for shadow only.
float2 generateProjectedPosition(float3 pos);
void sampleReflectionProbes(inout float3 ambenv, inout float3 glossenv,
    float2 tc, float3 pos, float3 norm, float glossiness, bool transparent, float3 amblit_linear);
// pbrBaseLight (deferredUtil.hlsl, attached here via isDeferred=true) does
// the real IBL(pbrIbl)+punctual(pbrPunctual) combine - the exact function
// softenLightF.hlsl's own real, already-working PBR lighting path uses.
// Reused here rather than re-deriving the BRDF math.
float3 pbrBaseLight(float3 diffuseColor, float3 specularColor, float metallic, float3 v, float3 norm, float perceptualRoughness, float3 light_dir, float3 sunlit, float scol, float3 radiance, float3 irradiance, float3 colorEmissive, float ao, float3 additive, float3 atten);
// S24: body in deferredUtil.hlsl (isDeferred=true); ports pbralphaF.glsl's
// LIGHT_LOOP(1..7) local point/spot light contribution.
float3 pbrCalcPointLightOrSpotLight(float3 diffuseColor, float3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    float3 n,
                    float3 p,
                    float3 v,
                    float3 lp,
                    float3 ld,
                    float3 lightColor,
                    float lightSize, float falloff, float is_pointlight, float ambiance);

struct PSInput
{
    // This struct's field order must exactly match pbralphaV.hlsl's
    // VSOutput, field for field - a TEXCOORD-slot mismatch here causes a
    // D3D11 "Signatures between stages are incompatible" VS/PS linkage error.
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float3 vary_fragcoord : TEXCOORD1;
    float2 base_color_texcoord : TEXCOORD2;
    float2 normal_texcoord : TEXCOORD3;
    float2 metallic_roughness_texcoord : TEXCOORD4;
    float2 emissive_texcoord : TEXCOORD5;
    float4 vertex_color : COLOR0;
    float3 vary_tangent : TEXCOORD6;
    nointerpolation float vary_sign : TEXCOORD7;
    float3 vary_normal : TEXCOORD8;
    bool isFrontFace : SV_IsFrontFace;
};

// Real implementation, replacing the debug stub (both this file and the
// upstream GL source, pbralphaF.glsl, were literally
// `frag_color = vec4(1.0, 0, 0.5, 0.5);` - never implemented on either
// backend, not a DX-specific regression). Mirrors pbropaqueF.hlsl's
// tangent-space normal reconstruction and pbrBaseLight()'s real IBL+punctual
// lighting combine; ambient comes from real sky/windlight atmospherics
// (calcAtmosphericVarsLinear), not a flat constant.
float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.vary_position);
    waterClip(IN.vary_position);

    float4 basecolor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord.xy).rgba;
    basecolor.rgb = srgb_to_linear(basecolor.rgb);
    basecolor *= IN.vertex_color;

#ifdef HAS_ALPHA_MASK
    if (basecolor.a < minimum_alpha)
        discard;
#endif

    float3 vNt = bumpMap.Sample(bumpMapSampler, IN.normal_texcoord.xy).xyz * 2.0 - 1.0;
    float sign = IN.vary_sign;
    float3 vN = normalize(IN.vary_normal);
    float3 vT = IN.vary_tangent.xyz;
    float3 vB = sign * cross(vN, vT);
    float3 norm = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);

    // S24: flips normal for back-facing polys (pbralphaF.glsl's gl_FrontFacing
    // equivalent) - hair/card geometry is routinely double-sided.
    norm *= IN.isFrontFace ? 1.0 : -1.0;

    // ORM texture: r=occlusion, g=roughness, b=metallic (standard glTF
    // packing, matches pbropaqueF.hlsl's identical convention).
    float3 orm = specularMap.Sample(specularMapSampler, IN.metallic_roughness_texcoord.xy).rgb;
    float ao = orm.r;
    // S24: floored well above pbrPunctual's own 8/255 minimum - avoids an
    // unbounded highlight on near-mirror surfaces (e.g. windows) when the
    // sun's reflection direction lines up with the view; kept as margin
    // even with real IBL/shadow wired in below.
    float perceptualRoughness = max(orm.g * roughnessFactor, 0.3);
    float metallic = orm.b * metallicFactor;

    float3 colorEmissive = emissiveColor;
    colorEmissive *= srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_texcoord.xy).rgb);

    float3 diffuseColor;
    float3 specularColor;
    calcDiffuseSpecular(basecolor.rgb, metallic, diffuseColor, specularColor);

    float3 pos = IN.vary_position;
    float3 v = -normalize(pos);
    float3 light_dir = (sun_up_factor == 1) ? sun_dir : moon_dir;

    float3 sunlit;
    float3 amblit;
    float3 atten;
    float3 additive;
    // S24: out-param order is (..., additive, atten) - atmosphericsFuncs.hlsl:196.
    // Don't swap: applySkyAndWaterFog()'s atmosFragLighting() treats additive
    // as an HDR additive sky-light term and atten as a plain 0-1 multiplier.
    calcAtmosphericVarsLinear(pos, norm, light_dir, sunlit, amblit, additive, atten);

    // S24: divide-by-z screen UV, adequate for the shadow lookup only - not
    // reused for the reflection-probe/SSR call below (see generateProjectedPosition()).
    float2 tc = IN.vary_fragcoord.xy / IN.vary_fragcoord.z * 0.5 + 0.5;

    float scol = 1.0;
#ifdef HAS_SUN_SHADOW
    scol = sampleDirectionalShadow(pos, norm, tc);
#endif

    // S24: irradiance seeded with amblit, then overwritten in place by
    // sampleReflectionProbes() - mirrors softenLightF.hlsl's PBR branch.
    float3 irradiance = amblit;
    float3 radiance = float3(0, 0, 0);
    float gloss = 1.0 - perceptualRoughness;
    // S24: perspective-correct screen UV, separate from tc above (needed for SSR).
    float2 probe_tc = generateProjectedPosition(pos);
    sampleReflectionProbes(irradiance, radiance, probe_tc, pos, norm, gloss, false, amblit);

    float3 color = pbrBaseLight(diffuseColor, specularColor, metallic, v, norm, perceptualRoughness, light_dir, sunlit, scol, radiance, irradiance, colorEmissive, ao, additive, atten);

    // S24: local point/spot light contribution, ported from pbralphaF.glsl's
    // LIGHT_LOOP(1..7) macro.
    float3 light = float3(0, 0, 0);
    light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm, pos, v, light_position[1].xyz, light_direction[1].xyz, light_diffuse[1].rgb, light_deferred_attenuation[1].x, light_deferred_attenuation[1].y, light_attenuation[1].z, light_attenuation[1].w);
    light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm, pos, v, light_position[2].xyz, light_direction[2].xyz, light_diffuse[2].rgb, light_deferred_attenuation[2].x, light_deferred_attenuation[2].y, light_attenuation[2].z, light_attenuation[2].w);
    light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm, pos, v, light_position[3].xyz, light_direction[3].xyz, light_diffuse[3].rgb, light_deferred_attenuation[3].x, light_deferred_attenuation[3].y, light_attenuation[3].z, light_attenuation[3].w);
    light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm, pos, v, light_position[4].xyz, light_direction[4].xyz, light_diffuse[4].rgb, light_deferred_attenuation[4].x, light_deferred_attenuation[4].y, light_attenuation[4].z, light_attenuation[4].w);
    light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm, pos, v, light_position[5].xyz, light_direction[5].xyz, light_diffuse[5].rgb, light_deferred_attenuation[5].x, light_deferred_attenuation[5].y, light_attenuation[5].z, light_attenuation[5].w);
    light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm, pos, v, light_position[6].xyz, light_direction[6].xyz, light_diffuse[6].rgb, light_deferred_attenuation[6].x, light_deferred_attenuation[6].y, light_attenuation[6].z, light_attenuation[6].w);
    light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm, pos, v, light_position[7].xyz, light_direction[7].xyz, light_diffuse[7].rgb, light_deferred_attenuation[7].x, light_deferred_attenuation[7].y, light_attenuation[7].z, light_attenuation[7].w);
    color += light;

    color = applySkyAndWaterFog(pos, additive, atten, float4(color, 1.0)).rgb;

    return float4(color, basecolor.a);
}

#else

// forward fullbright implementation for HUDs - no deferredUtil.hlsl
// dependency (isDeferred is never set for gHUDPBRAlphaProgram), so no
// waterClip/mirrorClip/pbrBaseLight/atmospherics here. Mirrors
// pbropaqueF.hlsl's own IS_HUD branch and pbralphaV.hlsl's matching
// simpler HUD VSOutput exactly.

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D emissiveMap : register(t1);
SamplerState emissiveMapSampler : register(s1);

uniform float3 emissiveColor;
// S24: MASK-mode-only, see the non-HUD branch's matching declaration.
#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()
#endif

float3 srgb_to_linear(float3 c);
float3 linear_to_srgb(float3 c);

struct PSInput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float2 base_color_texcoord : TEXCOORD1;
    float2 emissive_texcoord : TEXCOORD2;
    float4 vertex_color : COLOR0;
};

float4 main(PSInput IN) : SV_Target
{
    float4 basecolor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord.xy).rgba;
    basecolor.a *= IN.vertex_color.a;
#ifdef HAS_ALPHA_MASK
    if (basecolor.a < minimum_alpha)
        discard;
#endif

    float3 col = IN.vertex_color.rgb * srgb_to_linear(basecolor.rgb);
    float3 emissive = emissiveColor;
    emissive *= srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_texcoord.xy).rgb);
    col += emissive;

    return float4(linear_to_srgb(col), basecolor.a);
}

#endif
