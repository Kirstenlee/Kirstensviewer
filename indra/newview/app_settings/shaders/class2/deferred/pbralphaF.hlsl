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

// S24 (2026-08-09, task #172 fix): CTD on login - gHUDPBRAlphaProgram never
// sets mFeatures.isDeferred (only gDeferredPBRAlphaProgram/its rigged
// variant do, see llviewershadermgr.cpp), so deferredUtil.hlsl (which
// supplies waterClip()'s real body, plus pbrBaseLight/pbrIbl/pbrPunctual)
// is never attached to the HUD permutation. My first pass at this file
// called waterClip() unconditionally, which compiled fine for the two
// isDeferred=true variants but produced a hard D3DCompile failure ("function
// waterClip missing implementation") for "HUD PBR Alpha Shader" specifically
// - fatal at shader-load time during login, before any avatar/scene code
// runs. pbralphaV.hlsl's own IS_HUD branch confirms this isn't a corner
// case to paper over: it emits an entirely different, much simpler
// VSOutput (no normal/tangent/metallic-roughness texcoord/vary_fragcoord
// at all) for HUDs - matching pbropaqueF.hlsl's own real (already-working)
// #ifndef IS_HUD / #else split, mirrored here.
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
// S24 (task #226 follow-up, 2026-08-19): pbralphaF.glsl wraps this uniform
// (and its discard check below) in #ifdef HAS_ALPHA_MASK - MASK-mode PBR
// content only, per its own comment ("PBR alphaMode: MASK"). This HLSL copy
// had it unconditional - gDeferredPBRAlphaProgram (the BLEND-mode shader,
// used by any glTF material with alphaMode=BLEND, e.g. hair) never defines
// HAS_ALPHA_MASK (its C++ construction hardcodes DIFFUSE_ALPHA_MODE_BLEND,
// llviewershadermgr.cpp), so in GL this whole block compiles out entirely
// for BLEND mode. Under DX_RENDER it stayed active with whatever minimum_alpha
// happened to hold (C++ never had a reason to set it for a uniform GLSL
// never reads in this mode) - silently discarding any pixel below that
// threshold instead of letting it blend, hard-cutting exactly the soft,
// feathered, low-alpha edges alpha-BLEND content depends on (hair-card
// textures, etc.) into a hard rectangular silhouette. Root cause of
// "alpha not blending at all, producing a mess" on glTF-material BLEND-mode
// mesh (user-confirmed via screenshots, Z fight.PNG).
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
// S24 (task #173): matches alphaF.hlsl's identical forward-declaration -
// shadowUtil.hlsl supplies the real body (already attached here via
// isDeferred=true, same as every other consumer of this function).
#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen);
#endif
// S24 (task #173): real reflection-probe IBL, matching softenLightF.hlsl's
// PBR branch exactly.
//
// CORRECTION (2026-09-06, task #271): the claim below this comment used to
// make - that tc's divide-by-z approximation is "more than sufficient"
// because SSR "is deliberately not ported here" - was true only as long as
// RenderScreenSpaceReflectionGlossThreshold's old 0.9 default structurally
// excluded this material's glossiness (capped at 0.7) from ever reaching
// tapScreenSpaceReflection() at all. Now that the threshold gate is gone,
// SSR does run for this material, and the approximate tc (divide-by-.z,
// since pbralphaV.hlsl's vary_fragcoord never carries a real .w to divide
// by) sends its ray march from the wrong starting screen position - live-
// confirmed as "zero SSR contribution reaches the surface" once the
// threshold stopped hiding it. Fixed below: a separate, precisely-computed
// screen UV (generateProjectedPosition(), screenSpaceReflUtil.hlsl - the
// same proven-correct helper tapScreenSpaceReflection()'s own real callers
// use, including its non-obvious deliberate Y-flip) is used for the probe/
// SSR call specifically, leaving tc itself completely untouched for the
// shadow lookup above, which was never broken and doesn't need touching.
float2 generateProjectedPosition(float3 pos);
void sampleReflectionProbes(inout float3 ambenv, inout float3 glossenv,
    float2 tc, float3 pos, float3 norm, float glossiness, bool transparent, float3 amblit_linear);
// pbrBaseLight (deferredUtil.hlsl, attached here via isDeferred=true) does
// the real IBL(pbrIbl)+punctual(pbrPunctual) combine - the exact function
// softenLightF.hlsl's own real, already-working PBR lighting path uses.
// Reused here rather than re-deriving the BRDF math.
float3 pbrBaseLight(float3 diffuseColor, float3 specularColor, float metallic, float3 v, float3 norm, float perceptualRoughness, float3 light_dir, float3 sunlit, float scol, float3 radiance, float3 irradiance, float3 colorEmissive, float ao, float3 additive, float3 atten);
// S24 (2026-08-16, task #155/#157): real body already ported in
// deferredUtil.hlsl (attached here via isDeferred=true) - pbralphaF.glsl's
// LIGHT_LOOP(1..7) calling this was never carried over to this file at all,
// unlike the legacy (non-PBR) class2/deferred/alphaF.hlsl, which already
// calls the non-PBR calcPointLightOrSpotLight() successfully under
// DX_RENDER, proving light_position[]/light_direction[]/etc. are populated
// correctly for this draw pool. Any PBR-material rigged mesh (e.g. hair)
// got zero contribution from local point/spot lights until this fix.
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
    // CORRECTED (task #172): this struct's field order did not match
    // pbralphaV.hlsl's VSOutput at all (vary_position/vary_fragcoord were
    // swapped at TEXCOORD0/1, and vary_normal/vary_tangent/vary_sign were
    // shifted by one slot at TEXCOORD6-8) - the real cause of the D3D11
    // "Signatures between stages are incompatible" VS/PS linkage error
    // spamming every frame once rigged PBR-alpha content started actually
    // drawing (task #170). Now mirrors pbralphaV.hlsl's non-HUD VSOutput
    // exactly, field for field.
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
// S24 (task #173, 2026-08-11): real shadow-map sampling (sampleDirectionalShadow(),
// matching alphaF.hlsl's already-working pattern) and real reflection-probe
// IBL (sampleReflectionProbes(), matching softenLightF.hlsl's PBR branch)
// are both now wired in below. Both were previously believed to need a true
// perspective-divide-by-w screen UV this forward shader's vary_fragcoord
// can't provide (only xyz is stored, see pbralphaV.hlsl) - that assumption
// was wrong for both: shadow lookup already tolerates the coarser
// divide-by-z approximation, and the probe sample's tc parameter turned out
// to be completely unused inside doProbeSample() (confirmed by reading
// reflectionProbeF.hlsl directly - only SSR, not ported here, would need a
// real one). The white/blown-out "shiny" window look this was blocking
// (task #173) was near-fully-metallic materials with zero environment to
// reflect (radiance hardcoded to 0) - confirmed via a diagnostic packing
// metallic/roughness/scol into the output color before this fix landed.
float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.vary_position);
    waterClip(IN.vary_position);

    float4 basecolor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord.xy).rgba;
    basecolor.rgb = srgb_to_linear(basecolor.rgb);
    basecolor *= IN.vertex_color;

    // S24 (task #226 follow-up): see minimum_alpha's declaration comment
    // above - this discard is MASK-mode-only in the real GLSL, unconditional
    // here was the bug.
#ifdef HAS_ALPHA_MASK
    if (basecolor.a < minimum_alpha)
        discard;
#endif

    // S24 (2026-08-09, task #173): diagnostic bisection (return basecolor;)
    // confirmed texture sampling/binding is correct - grey/flat/no-shine
    // was the raw unlit texture, exactly as expected with lighting skipped.
    // Real lighting restored below.
    float3 vNt = bumpMap.Sample(bumpMapSampler, IN.normal_texcoord.xy).xyz * 2.0 - 1.0;
    float sign = IN.vary_sign;
    float3 vN = normalize(IN.vary_normal);
    float3 vT = IN.vary_tangent.xyz;
    float3 vB = sign * cross(vN, vT);
    float3 norm = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);

    // S24 (2026-08-16, task #155/#157): pbralphaF.glsl:156 flips the normal
    // for back-facing polygons (`norm *= gl_FrontFacing ? 1.0 : -1.0;`) -
    // absent entirely from this file until now. Hair and similar PBR
    // attachments are routinely authored as double-sided planar/card
    // geometry; without this, back-facing triangles lit with an inward-
    // pointing normal (dark/wrong-shaded from the "inside"). Same
    // SV_IsFrontFace idiom already proven working in pbrterrainF.hlsl.
    norm *= IN.isFrontFace ? 1.0 : -1.0;

    // ORM texture: r=occlusion, g=roughness, b=metallic (standard glTF
    // packing, matches pbropaqueF.hlsl's identical convention).
    float3 orm = specularMap.Sample(specularMapSampler, IN.metallic_roughness_texcoord.xy).rgb;
    float ao = orm.r;
    // S24 (2026-08-09, task #173): floored well above pbrPunctual's own
    // 8/255 minimum. Originally added because this v1 forward-alpha path
    // had no reflection-probe IBL - a near-mirror surface with nothing to
    // reflect produced a genuine unbounded highlight on large flat surfaces
    // like windows whenever the sun's reflection direction lined up with
    // the view (confirmed via user report, "windows render solid white",
    // and a follow-up diagnostic that traced it to exactly this: near-fully
    // metallic "shiny" materials with radiance hardcoded to 0).
    // S24 (2026-08-11): both real shadow attenuation (scol) and real
    // reflection-probe IBL (radiance) are now wired in below, which
    // together address the actual root cause this floor was working around
    // - kept as an extra margin rather than removed outright, since
    // shadow-map visual correctness itself is still a separate, ongoing
    // investigation (task #186). Revisit removing/relaxing this once both
    // are confirmed solid by the user's next build.
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
    // S24 (2026-08-11, task #173): REAL ROOT CAUSE of the white/black
    // diagnostic contradiction (bright output despite a black pbrBaseLight()
    // input) - this call had additive/atten swapped relative to the actual
    // definition (atmosphericsFuncs.hlsl:196, out order is
    // ..., additive, atten), matching alphaF.hlsl's already-correct call
    // exactly wrong. atmosFragLighting() (called via applySkyAndWaterFog()
    // below) treats its 2nd arg as an HDR additive sky-light term (gamma-
    // expanded, doubled, HDR-scaled) and its 3rd as a plain 0-1
    // attenuation multiplier - with the swap, the real attenuation value
    // (normally a modest fraction) was being fed through the HDR-additive
    // path instead, producing a bright result completely independent of
    // the surface's actual lit color. Fixed to match the real signature.
    calcAtmosphericVarsLinear(pos, norm, light_dir, sunlit, amblit, additive, atten);

    // S24 (task #173): tc feeds the shadow lookup below - divide-by-z
    // screen UV, same as alphaF.hlsl's already-working shadow pattern (see
    // pbralphaV.hlsl's near_clip bias). Sufficient for the shadow map, which
    // tolerates the approximation - see the forward-declaration comment
    // above for why this is NOT reused for the reflection-probe/SSR call
    // below anymore.
    float2 tc = IN.vary_fragcoord.xy / IN.vary_fragcoord.z * 0.5 + 0.5;

    float scol = 1.0;
#ifdef HAS_SUN_SHADOW
    scol = sampleDirectionalShadow(pos, norm, tc);
#endif

    // S24 (2026-08-11, task #173): real reflection-probe IBL - diagnostic
    // (packed metallic/roughness/scol into the output color) confirmed
    // these are near-fully-metallic ("shiny") materials; with radiance
    // hardcoded to 0 they had literally no environment to reflect, which is
    // what produced the blown-out/white look, independent of shadow
    // attenuation. irradiance is seeded with the atmospherics ambient
    // (amblit) and then overwritten in place by sampleReflectionProbes()
    // with a real probe-sampled value, exactly mirroring softenLightF.hlsl's
    // PBR branch.
    float3 irradiance = amblit;
    float3 radiance = float3(0, 0, 0);
    float gloss = 1.0 - perceptualRoughness;
    // S24 (2026-09-06, task #271): real perspective-correct screen UV,
    // separate from tc above - see the forward-declaration comment near
    // sampleReflectionProbes()'s own declaration for why.
    float2 probe_tc = generateProjectedPosition(pos);
    sampleReflectionProbes(irradiance, radiance, probe_tc, pos, norm, gloss, false, amblit);

    float3 color = pbrBaseLight(diffuseColor, specularColor, metallic, v, norm, perceptualRoughness, light_dir, sunlit, scol, radiance, irradiance, colorEmissive, ao, additive, atten);

    // S24 (2026-08-16, task #155/#157): local point/spot light contribution,
    // ported from pbralphaF.glsl's LIGHT_LOOP(1..7) macro - see the
    // pbrCalcPointLightOrSpotLight forward-declaration comment above for why
    // this was missing entirely.
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
// S24 (task #226 follow-up): see the non-HUD branch's matching comment -
// MASK-mode-only in the real GLSL, was unconditional here.
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
