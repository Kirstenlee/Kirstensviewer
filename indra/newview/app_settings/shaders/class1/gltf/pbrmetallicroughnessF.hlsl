/**
 * @file class1/gltf/pbrmetallicroughnessF.hlsl
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

// GLTF pbrMetallicRoughness implementation

uniform int gltf_material_id;

// HLSL globals are implicitly const unless declared static - GLSL has no
// such rule, these are written to below in unpackMaterial().
static float3 emissiveColor = float3(0,0,0);
static float metallicFactor = 1.0;
static float roughnessFactor = 1.0;
static float minimum_alpha = -1.0;

cbuffer GLTFMaterials : register(b0)
{
    float4 gltf_material_data[MAX_UBO_VEC4S];
};

void unpackMaterial()
{
    if (gltf_material_id > -1)
    {
        int idx = gltf_material_id * 12;
        emissiveColor = gltf_material_data[idx + 10].rgb;
        roughnessFactor = gltf_material_data[idx + 11].g;
        metallicFactor = gltf_material_data[idx + 11].b;
        minimum_alpha -= gltf_material_data[idx + 11].a;
    }
}

// t0-t3/s0-s3 are reserved by deferredUtil.hlsl's normalMap/depthMap/
// projectionMap/brdfLut (attached here via isDeferred on the alpha-blend
// variant) - moved this file's own textures to t5-t9/s5-s9 to avoid
// collision, same pattern as materialF.hlsl/pbralphaF.hlsl. Also renamed
// this file's own "normalMap" to "gltfNormalMap" - it collided by NAME
// with deferredUtil.hlsl's normalMap even before the register move (two
// genuinely different resources that happened to share a name, not a
// duplicate of the same one - same shape as the earlier depthMap/
// waterDepthMap rename).
//
// CORRECTION (2026-09-02): the comment above used to also claim t4/s4
// was reserved by reflectionProbeF.hlsl's environmentMap - false, real
// X4500 "overlapping register semantics" compile failure traced it:
// environmentMap/environmentMapSampler are actually at t9/s9 (see that
// file's own declaration), not t4/s4. This file's occlusionMap was
// sitting at t9/s9 too, directly colliding. t4/s4 was never actually
// used by anything this shader attaches - moved occlusionMap there for
// real (freeing t9/s9 for reflectionProbeF.hlsl's genuine use).
Texture2D diffuseMap : register(t5);
SamplerState diffuseMapSampler : register(s5);
Texture2D emissiveMap : register(t6);
SamplerState emissiveMapSampler : register(s6);

void mirrorClip(float3 pos);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);
float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

#ifndef UNLIT
Texture2D gltfNormalMap : register(t7);
SamplerState gltfNormalMapSampler : register(s7);
Texture2D metallicRoughnessMap : register(t8);
SamplerState metallicRoughnessMapSampler : register(s8);
Texture2D occlusionMap : register(t4);
SamplerState occlusionMapSampler : register(s4);
#endif

// S24 (2026-08-19, task #238, task #227 audit finding): this whole
// ALPHA_BLEND declaration block was missing entirely - pbrmetallicroughnessF.glsl
// has a full #ifdef ALPHA_BLEND lit-forward-shading branch (punctual+IBL PBR
// lighting, shadow sampling, sky/water fog) that this HLSL file never had any
// trace of; the ALPHA_BLEND permutation instead silently fell through to the
// G-buffer/MRT branch below, which writes SV_Target0-3, not the single
// forward-blended SV_Target this permutation actually needs - a real, live
// gap (gltfscenemanager.cpp sets GLTFVariant::ALPHA_BLEND for any non-opaque
// GLTF material). Ported faithfully below, reusing the exact same shared
// functions (deferredUtil.hlsl/reflectionProbeF.hlsl/shadowUtil.hlsl, all
// attached here since isDeferred/hasReflectionProbes/hasShadows are all set
// for this variant - see make_gltf_variant(), llviewershadermgr.cpp) that
// class2/deferred/pbralphaF.hlsl's own already-working equivalent uses.
#ifdef ALPHA_BLEND
// S24 (2026-09-02): a local clipPlane/clipSign pair used to be declared
// here, copied in during task #238's port - real D3DCompile failure
// (X3003 redefinition of 'clipPlane', this shader) traced it to
// globalF.hlsl's own clipPlane/clipSign (used by mirrorClip(), always
// attached to every fragment shader per that file's own comment).
// Confirmed genuinely dead here: waterClip()'s real body (deferredUtil.hlsl)
// uses waterPlane/waterSign, not clipPlane/clipSign at all, and
// pbralphaF.hlsl - the file this whole block was "ported faithfully" from
// - never declared this pair either. Same bug class already hit and fixed
// once for heroClipPlane (see reflectionProbeF.hlsl's own comment).
void waterClip(float3 pos);
void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);
float4 applySkyAndWaterFog(float3 pos, float3 additive, float3 atten, float4 color);

#ifndef UNLIT
#ifdef HAS_SUN_SHADOW
Texture2D lightMap : register(t10);
SamplerState lightMapSampler : register(s10);
// S24 (2026-09-02): real D3DCompile failure (X3003 redefinition of
// 'screen_res') - this file also declares inv_proj/screen_res later, as a
// pair, under LL_INV_PROJ_DECLARED (matching deferredUtil.hlsl's pair).
// This line was copied in alone during task #238's ALPHA_BLEND port,
// unguarded, before that guard existed further down. Fixed by declaring
// the SAME pair here under the SAME guard (not just screen_res alone) -
// this block is textually first when HAS_SUN_SHADOW is defined, so a
// lone screen_res-only declaration would claim the guard macro and
// silently skip the later block's inv_proj declaration entirely, leaving
// it undeclared wherever this file actually uses it.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif
#endif

uniform float4 light_position[8];
uniform float3 light_direction[8];
uniform float4 light_attenuation[8];
uniform float3 light_diffuse[8];
uniform float2 light_deferred_attenuation[8];

#ifndef LL_SUN_UP_FACTOR_DECLARED
#define LL_SUN_UP_FACTOR_DECLARED
uniform int sun_up_factor;
#endif
#ifndef LL_SUN_MOON_DIR_DECLARED
#define LL_SUN_MOON_DIR_DECLARED
uniform float3 sun_dir;
uniform float3 moon_dir;
#endif

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen);
#endif
void sampleReflectionProbes(inout float3 ambenv, inout float3 glossenv,
    float2 tc, float3 pos, float3 norm, float glossiness, bool transparent, float3 amblit_linear);
void calcDiffuseSpecular(float3 baseColor, float metallic, inout float3 diffuseColor, inout float3 specularColor);
float3 pbrBaseLight(float3 diffuseColor, float3 specularColor, float metallic, float3 v, float3 norm, float perceptualRoughness, float3 light_dir, float3 sunlit, float scol, float3 radiance, float3 irradiance, float3 colorEmissive, float ao, float3 additive, float3 atten);
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
#endif // !UNLIT
#endif // ALPHA_BLEND

#include "varying/pbrMetallicRoughnessVarying.hlsli"

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input.
// Shared by both main() variants below (UNLIT and the full G-buffer path).
// isFrontFace added task #238 - see the lit main()'s norm-flip comment.
struct PSInput
{
    float4 position : SV_Position;
    PBRMetallicRoughnessVarying varying;
    bool isFrontFace : SV_IsFrontFace;
};

#ifdef UNLIT
float4 main(PSInput IN) : SV_Target
{
    unpackMaterial();

    float4 baseColor = diffuseMap.Sample(diffuseMapSampler, IN.varying.base_color_uv);
    baseColor.rgb = srgb_to_linear(baseColor.rgb);
    baseColor *= IN.varying.vertex_color;

    if (baseColor.a < minimum_alpha)
    {
        discard;
    }

    float4 color = baseColor;
    color.rgb += emissiveColor * srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.varying.emissive_uv).rgb);

    float4 frag_color = max(color, float4(0, 0, 0, 0));
    return frag_color;
}
#elif defined(ALPHA_BLEND)
// S24 (2026-08-19, task #238): real lit forward-blend path - see the
// declaration block above's comment for the full root-cause writeup.
// Mirrors class2/deferred/pbralphaF.hlsl's already-working, already-live-
// tested equivalent function-for-function; only the varying/uniform names
// differ (this file's own GLTF material-uniform unpacking, base_color_uv
// etc.) and the a=basecolor.a*vertex_color.a line at the end intentionally
// re-multiplies by vertex_color.a a second time (basecolor.a already
// includes it from the `baseColor *= IN.varying.vertex_color;` line above)
// - matches pbrmetallicroughnessF.glsl line-for-line even though this looks
// odd; not this port's place to "fix" upstream's own arithmetic.
float4 main(PSInput IN) : SV_Target
{
    unpackMaterial();
    float3 pos = IN.varying.vary_position;
    mirrorClip(pos);

    float4 baseColor = diffuseMap.Sample(diffuseMapSampler, IN.varying.base_color_uv);
    baseColor.rgb = srgb_to_linear(baseColor.rgb);
    baseColor *= IN.varying.vertex_color;

    if (baseColor.a < minimum_alpha)
    {
        discard;
    }

    float3 emissive = emissiveColor;
    emissive *= srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.varying.emissive_uv).rgb);

    float3 vNt = gltfNormalMap.Sample(gltfNormalMapSampler, IN.varying.normal_uv).xyz * 2.0 - 1.0;
    float sign = IN.varying.vary_sign;
    float3 vN = normalize(IN.varying.vary_normal);
    float3 vT = IN.varying.vary_tangent;
    float3 vB = sign * cross(vN, vT);
    float3 norm = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);
    // S24 (task #238): pbrmetallicroughnessF.glsl:221 flips the normal for
    // back-facing polygons (`norm *= gl_FrontFacing ? 1.0 : -1.0;`) - same
    // idiom already proven working in pbralphaF.hlsl/pbrterrainF.hlsl.
    norm *= IN.isFrontFace ? 1.0 : -1.0;

    float3 orm = metallicRoughnessMap.Sample(metallicRoughnessMapSampler, IN.varying.metallic_roughness_uv).rgb;
    orm.r = occlusionMap.Sample(occlusionMapSampler, IN.varying.occlusion_uv).r;
    orm.g *= roughnessFactor;
    orm.b *= metallicFactor;

    float scol = 1.0;
    float3 light_dir = (sun_up_factor == 1) ? sun_dir : moon_dir;

    float3 sunlit, amblit, additive, atten;
    calcAtmosphericVarsLinear(pos, norm, light_dir, sunlit, amblit, additive, atten);
    float3 sunlit_linear = srgb_to_linear(sunlit);

    float2 frag = IN.varying.vary_fragcoord.xy / IN.varying.vary_fragcoord.z * 0.5 + 0.5;

#ifdef HAS_SUN_SHADOW
    scol = sampleDirectionalShadow(pos, norm, frag);
#endif

    float perceptualRoughness = orm.g;
    float metallic = orm.b;

    float gloss = 1.0 - perceptualRoughness;
    float3 irradiance = float3(0, 0, 0);
    float3 radiance = float3(0, 0, 0);
    sampleReflectionProbes(irradiance, radiance, IN.varying.vary_position.xy * 0.5 + 0.5, pos, norm, gloss, true, amblit);

    float3 diffuseColor, specularColor;
    calcDiffuseSpecular(baseColor.rgb, metallic, diffuseColor, specularColor);

    float3 v = -normalize(pos);

    float3 color = pbrBaseLight(diffuseColor, specularColor, metallic, v, norm, perceptualRoughness, light_dir, sunlit_linear, scol, radiance, irradiance, emissive, orm.r, additive, atten);

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

    float a = baseColor.a * IN.varying.vertex_color.a;

    return max(float4(color, a), float4(0, 0, 0, 0));
}
#else
struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;
    unpackMaterial();
    mirrorClip(IN.varying.vary_position);

    float4 baseColor = diffuseMap.Sample(diffuseMapSampler, IN.varying.base_color_uv);
    baseColor.rgb = srgb_to_linear(baseColor.rgb);
    baseColor *= IN.varying.vertex_color;

    if (baseColor.a < minimum_alpha)
    {
        discard;
    }

    float3 n = normalize(IN.varying.vary_normal);
    float3 t = normalize(IN.varying.vary_tangent);
    float3 b = IN.varying.vary_sign * cross(n, t);
    float3 tnorm = normalize(gltfNormalMap.Sample(gltfNormalMapSampler, IN.varying.normal_uv).xyz * 2.0 - 1.0);
    tnorm = normalize(tnorm.x * t + tnorm.y * b + tnorm.z * n);
    // S24 (task #238, found alongside the ALPHA_BLEND port): this G-buffer
    // path was also missing pbrmetallicroughnessF.glsl:221's back-facing
    // normal flip (`norm *= gl_FrontFacing ? 1.0 : -1.0;`) - not flagged by
    // the task #227 audit (it only caught the missing ALPHA_BLEND branch),
    // found while re-reading this file for that fix. Same idiom already
    // proven working in pbralphaF.hlsl/pbrterrainF.hlsl.
    tnorm *= IN.isFrontFace ? 1.0 : -1.0;

    float3 orm = metallicRoughnessMap.Sample(metallicRoughnessMapSampler, IN.varying.metallic_roughness_uv).rgb;
    float occlusion = occlusionMap.Sample(occlusionMapSampler, IN.varying.occlusion_uv).r;
    float perceptualRoughness = orm.g * roughnessFactor;
    float metallic = orm.b * metallicFactor;

    float3 emissive = emissiveColor * srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.varying.emissive_uv).rgb);

    float3 col = baseColor.rgb;

    OUT.data0 = max(float4(col, 0.0), float4(0, 0, 0, 0));
    OUT.data1 = max(float4(occlusion, perceptualRoughness, metallic, 0.0), float4(0, 0, 0, 0));
    OUT.data2 = encodeNormal(tnorm, 0, GBUFFER_FLAG_HAS_PBR);

#if defined(HAS_EMISSIVE)
    OUT.data3 = max(float4(emissive, 0), float4(0, 0, 0, 0));
#endif
    return OUT;
}
#endif
