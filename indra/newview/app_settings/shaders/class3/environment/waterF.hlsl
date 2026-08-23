/**
 * @file class3/environment/waterF.hlsl
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

// S24 (2026-08-09, task #143): real port of class3/environment/waterF.glsl -
// the previous body here was a placeholder (`return float4(0,0,0,1)`), so
// the above-water surface (the normal view looking down at water) rendered
// solid opaque black unconditionally. This is the real 375-line
// implementation, ported to HLSL with the same "improve where it makes
// sense, no requirement to stay slavish to GL" latitude already used
// elsewhere this session - dead/unused forward declarations present in the
// GLSL source (scaleSoftClipFragLinear, BRDF, pbrIbl, pbrBaseLight,
// linear_to_srgb, atmosLighting, scaleSoftClip, toneMapNoExposure - none
// of these are actually called from main()) were dropped rather than
// carried over as dead weight.
#define WATER_MINIMAL 1

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen);
#endif

void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);
float4 applyWaterFogViewLinear(float3 pos, float4 color);

void mirrorClip(float3 pos);

void calcDiffuseSpecular(float3 baseColor, float metallic, inout float3 diffuseColor, inout float3 specularColor);

void pbrPunctual(float3 diffuseColor, float3 specularColor, float perceptualRoughness, float metallic, float3 n, float3 v, float3 l, out float nl, out float3 diff, out float3 spec);

void sampleReflectionProbesWater(inout float3 ambenv, inout float3 glossenv,
        float2 tc, float3 pos, float3 norm, float glossiness, float3 amblit_linear);

float3 getPositionWithNDC(float3 ndc);
float getDepth(float2 pos_screen);

float3 srgb_to_linear(float3 c);

// S24 (2026-08-09, task #144 register sweep): moved off t0-t4 (which
// collided with deferredUtil.hlsl's normalMap/depthMap/projectionMap/
// brdfLut at t0-t3 - both attached here since gWaterProgram sets
// hasReflectionProbes=true, per attachShaderFeatures()'s
// isDeferred||hasReflectionProbes condition, llshadermgr.cpp:214) into
// t5-t8 for water's own 4 non-exclusion textures. exclusionTex originally
// went to t9 to match the alpha shaders' established free zone (t5-t9),
// but a real build (2026-08-09) surfaced a genuine X4500 "overlapping
// register semantics" collision there: reflectionProbeF.hlsl's
// environmentMap (also attached via hasReflectionProbes) is actually at
// t9/s9, not t4 as an earlier comment (materialF.hlsl family) suggested -
// verified directly against this shader's own resolved/dumped source
// (t0-t3 deferredUtil, t5-t8 water's own, t9 environmentMap, t10-t15
// shadowUtil - t4 is the one genuinely free slot). exclusionTex moved
// there instead.
Texture2D bumpMap : register(t5);
Texture2D bumpMap2 : register(t6);
SamplerState bumpMapSampler : register(s5);
SamplerState bumpMap2Sampler : register(s6);
uniform float blend_factor;

#ifdef TRANSPARENT_WATER
Texture2D screenTex : register(t7);
SamplerState screenTexSampler : register(s7);
// S24 (2026-08-09, task #123): the original GLSL declares its own
// "depthMap" uniform directly, sampled with a plain texture() call - that
// works under GL because separate-compile-then-link tolerates identical
// uniform redeclarations across attached files (same reasoning documented
// throughout this session for other dual-declared uniforms). It does NOT
// work here: deferredUtil.hlsl (attached since gWaterProgram sets
// hasReflectionProbes=true) already declares its own "depthMap" at t1 -
// a second same-named Texture2D would be a duplicate-symbol compile
// error, which is why an earlier version of this file renamed its copy to
// "waterDepthMap" at a different register to dodge the collision.
//
// That rename silently broke the actual depth data path: LLPipeline::
// bindDeferredShader(shader, nullptr, &mWaterDis) (dxdrawpoolwater.cpp) -
// which this pool calls specifically to redirect water's depth read to
// the mWaterDis snapshot instead of the live G-buffer - works by finding
// a texture named exactly "depthMap" (LLShaderMgr::DEFERRED_DEPTH's
// reserved name, llshadermgr.cpp:1580) and binding mWaterDis there. A
// texture named "waterDepthMap" doesn't match that lookup, so it was
// never bound by anything - real, confirmed root cause of "the reflection
// fights the water / self-referencing screen-copy hazard" once this
// shader had real Sample() calls to expose it (the old stub never
// sampled it, so this never surfaced).
//
// Real fix: don't declare a second depth texture at all - use
// deferredUtil.hlsl's own already-attached, already-correctly-bound
// getDepth() (forward-declared below), the same function pointLightF.hlsl/
// spotLightF.hlsl/softenLightF.hlsl already use for exactly this purpose.
// It already applies the GL-vs-D3D11 texture-origin flip internally too,
// so no manual "1.0 - y" needed at the call sites below.
#endif

Texture2D exclusionTex : register(t4);
SamplerState exclusionTexSampler : register(s4);

uniform float3 lightDir;
uniform float3 specular;
uniform float blurMultiplier;
uniform float refScale;
uniform float kd;
uniform float3 normScale;
uniform float fresnelScale;
uniform float fresnelOffset;

// S24 - Advanced water material controls (unlock hardcoded values)
uniform float waterMetallic;
uniform float waterRoughnessOverride;
uniform float waterSpecularIntensity;
uniform float waterReflectionIntensity;

// S24 - Advanced artistic controls (Phase 1 & 2)
uniform float3 waterColorTint;
uniform float waterColorTintAlpha;
uniform float waterFresnelPower;
uniform float waterWaveSpeed;           // Applied in C++ to phase_time
uniform float waterShoreFadeDistance;
uniform float waterUnderwaterFogMult;   // Applied in C++ to fog_density
uniform float waterReflectionWarmth;

struct PSInput
{
    float4 position : SV_Position;

    // refCoord.w is the real, unmodified clip W (see waterV.hlsl's own
    // comment) - used below for a true perspective divide. bigWave is
    // (bigWaveX, view.w).
    float4 refCoord : TEXCOORD0;
    float4 littleWave : TEXCOORD1;
    float4 view : TEXCOORD2;
    float3 vary_position : TEXCOORD3;
    float3 vary_light_dir : TEXCOORD4;
    float3 vary_tangent : TEXCOORD5;
    float3 vary_normal : TEXCOORD6;
    float bigWaveX : TEXCOORD7;
};

float2 getScreenCoord(float4 clip);

// S24: HLSL globals default to implicitly-const (X3025) unless marked
// static - unlike GLSL, where a plain global is ordinary mutable storage.
// vN/vT/vB are written to in main() below (transform_normal() reads them
// back), so this needs "static" to compile at all.
static float3 vN, vT, vB;

float3 transform_normal(float3 vNt)
{
    return normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);
}

float3 BlendNormal(float3 bump1, float3 bump2)
{
    return lerp(bump1, bump2, blend_factor);
}

void generateWaveNormals(PSInput IN, out float3 wave1, out float3 wave2, out float3 wave3)
{
    // Generate all of our wave normals.
    // We layer these back and forth.

    float2 bigwave = float2(IN.bigWaveX, IN.view.w);

    float3 wave1_a = bumpMap.Sample(bumpMapSampler, bigwave).xyz * 2.0 - 1.0;
    float3 wave2_a = bumpMap.Sample(bumpMapSampler, IN.littleWave.xy).xyz * 2.0 - 1.0;
    float3 wave3_a = bumpMap.Sample(bumpMapSampler, IN.littleWave.zw).xyz * 2.0 - 1.0;

    float3 wave1_b = bumpMap2.Sample(bumpMap2Sampler, bigwave).xyz * 2.0 - 1.0;
    float3 wave2_b = bumpMap2.Sample(bumpMap2Sampler, IN.littleWave.xy).xyz * 2.0 - 1.0;
    float3 wave3_b = bumpMap2.Sample(bumpMap2Sampler, IN.littleWave.zw).xyz * 2.0 - 1.0;

    wave1 = BlendNormal(wave1_a, wave1_b);
    wave2 = BlendNormal(wave2_a, wave2_b);
    wave3 = BlendNormal(wave3_a, wave3_b);
}

void calculateFresnelFactors(out float3 df3, out float2 df2, float3 viewVec, float3 wave1, float3 wave2, float3 wave3, float3 wavef)
{
    // We calculate the fresnel here.
    // We do this by getting the dot product for each sets of waves, and applying scale and offset.

    df3 = max(float3(0, 0, 0), float3(
        dot(viewVec, wave1),
        dot(viewVec, (wave2 + wave3) * 0.5),
        dot(viewVec, wave3)
    ) * fresnelScale + fresnelOffset);

    // S24 Advanced - Unlock hardcoded power-of-2, use user-controlled fresnel power
    df3 = pow(df3, float3(waterFresnelPower, waterFresnelPower, waterFresnelPower)); // Was: df3 *= df3 (hardcoded power of 2)

    df2 = max(float2(0, 0), float2(
        df3.x + df3.y + df3.z,
        dot(viewVec, wavef) * fresnelScale + fresnelOffset
    ));
}

float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.vary_position);

    vN = IN.vary_normal;
    vT = IN.vary_tangent;
    vB = cross(vN, vT);

    float3 pos = IN.vary_position.xyz;
    float dist = length(pos.xyz);

    //normalize view vector
    float3 viewVec = normalize(pos.xyz);

    // Setup our waves.
    float3 wave1 = float3(0, 0, 1);
    float3 wave2 = float3(0, 0, 1);
    float3 wave3 = float3(0, 0, 1);

    generateWaveNormals(IN, wave1, wave2, wave3);

    float dmod = sqrt(dist);
    // S24 (2026-08-09, task #146): the original GLSL divides refCoord.xy
    // by refCoord.z (not w) to approximate a screen-space UV - a trick
    // that happens to work under GL's -w..w clip-space Z range, but
    // produces a warped ("fisheye"), mispositioned result under D3D11's
    // 0..w range (confirmed via a real in-world test - solid reflection-
    // shaped color with no visible wave perturbation, since the base UV
    // was already wrong enough to swamp the small ripple offset). Fixed
    // by using the real clip W via the same getScreenCoord() every other
    // converted shader already uses for this exact purpose (pointLightF.hlsl/
    // spotLightF.hlsl/softenLightF.hlsl) - a true perspective divide,
    // API-convention-independent. distort itself stays unflipped (used
    // below for reflection-probe/refraction lookups that already apply
    // the flip at their own .Sample() sites, and for getPositionWithNDC()'s
    // NDC reconstruction, which must not be flipped).
    float2 distort = getScreenCoord(IN.refCoord);

    float3 wavef = (wave1 + wave2 * 0.4 + wave3 * 0.6) * 0.5;

    float3 df3 = float3(0, 0, 0);
    float2 df2 = float2(0, 0);

    float3 sunlit;
    float3 amblit;
    float3 additive;
    float3 atten;
    calcAtmosphericVarsLinear(pos.xyz, wavef, IN.vary_light_dir, sunlit, amblit, additive, atten);

    calculateFresnelFactors(df3, df2, normalize(IN.view.xyz), wave1, wave2, wave3, wavef);

    float3 waver = wavef * 3;

    float3 up = transform_normal(float3(0, 0, 1));
    float vdu = -dot(viewVec, up) * 2;

    float3 wave_ibl = wavef * normScale;
    wave_ibl.z *= 2.0;
    wave_ibl = transform_normal(normalize(wave_ibl));

    float3 norm = transform_normal(normalize(wavef));

    vdu = clamp(vdu, 0, 1);

    wavef = normalize(wavef);
    wavef = transform_normal(wavef);

    dist = max(dist, 5.0);

    //figure out distortion vector (ripply)
    float2 distort2 = distort + waver.xy * refScale / max(dmod, 1.0) * 2;
    distort2 = clamp(distort2, float2(0, 0), float2(0.999, 0.999));

    float shadow = 1.0f;

    // S24 (origin sweep): exclusionTex is a real D3D11 render target
    // (mWaterExclusionMask) - flip at the sample site, distort itself
    // stays unflipped (also feeds sampleDirectionalShadow()/reflection
    // probes below).
    float water_mask = exclusionTex.Sample(exclusionTexSampler, float2(distort.x, 1.0 - distort.y)).r;

#ifdef HAS_SUN_SHADOW
    shadow = sampleDirectionalShadow(pos.xyz, norm.xyz, distort);
#endif

    float3 sunlit_linear = sunlit;
    float fade = 1;
#ifdef TRANSPARENT_WATER
    // S24 (task #123): getDepth() reads mWaterDis's depth (bound via this
    // pool's bindDeferredShader(shader, nullptr, &mWaterDis) call - see
    // this file's header comment on why a separately-declared texture
    // here doesn't work) and already applies the origin flip internally.
    float depth = getDepth(distort);

    float3 refPos = getPositionWithNDC(float3(distort * 2.0 - float2(1.0, 1.0), depth * 2.0 - 1.0));

    // Calculate some distance fade in the water to better assist with refraction blending and reducing the refraction texture's "disconnect".
    fade = max(0, min(1, (pos.z - refPos.z) / 10));

    fade *= water_mask;
    distort2 = lerp(distort, distort2, min(1, fade * 10));
    depth = getDepth(distort2);

    refPos = getPositionWithNDC(float3(distort2 * 2.0 - float2(1.0, 1.0), depth * 2.0 - 1.0));

    if (pos.z < refPos.z - 0.05)
    {
        distort2 = distort;
    }

    float4 fb = screenTex.Sample(screenTexSampler, float2(distort2.x, 1.0 - distort2.y));

#else
    float4 fb = applyWaterFogViewLinear(viewVec * 2048.0, float4(1.0, 1.0, 1.0, 1.0));

    if (water_mask < 1)
        discard;
#endif

    // S24 - Use controllable water material properties instead of hardcoded values
    float metallic = waterMetallic; // Was: 1.0 HARDCODED
    float perceptualRoughness = waterRoughnessOverride > 0.0 ? waterRoughnessOverride : blurMultiplier;
    float gloss = 1 - perceptualRoughness;

    float3 irradiance = float3(0, 0, 0);
    float3 radiance = float3(0, 0, 0);
    // S24 (2026-08-09, task #147 step 0): re-enabled. The claim in the
    // comment this replaced - "environmentMap/t9 is never bound under
    // DX_RENDER" - is now STALE: LLPipeline::bindDeferredShader()
    // (pipeline.cpp:8823-8862, task #113, 2026-08-06) force-binds the
    // legacy single-cubemap environmentMap/t9 for any shader with
    // mFeatures.hasReflectionProbes, which gWaterProgram has - confirmed
    // by direct read, not assumed. sampleReflectionProbesWater() ->
    // sampleReflectionProbes() (reflectionProbeF.hlsl) already samples
    // exactly that texture correctly. This is step 0 of task #147's real
    // capture-pipeline work (see llreflectionmapmanager.cpp) - a fast,
    // independent test of the legacy-env-map plumbing before the larger
    // LLCubeMapArray-based per-probe work lands. Expected result: a real
    // but non-per-position (single static sky cubemap) reflection instead
    // of flat black; per-probe accuracy arrives once the array pipeline
    // and reflectionProbeF.hlsl's v1 TextureCubeArray sample are in.
    sampleReflectionProbesWater(irradiance, radiance, distort2, pos.xyz, wave_ibl.xyz, gloss, amblit);

    float3 diffuseColor = float3(0, 0, 0);
    float3 specularColor = float3(0, 0, 0);
    float3 specular_linear = srgb_to_linear(specular);
    calcDiffuseSpecular(specular_linear, metallic, diffuseColor, specularColor);

    float3 v = -normalize(pos.xyz);

    float NdotV = clamp(abs(dot(norm, v)), 0.001, 1.0);

    float nl = 0;
    float3 diffPunc = float3(0, 0, 0);
    float3 specPunc = float3(0, 0, 0);

    float3 light_dir = transform_normal(lightDir);

    pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, normalize(wavef + up * max(dist, 32.0) / 32.0 * (1.0 - vdu)), v, normalize(light_dir), nl, diffPunc, specPunc);

    // S24 - Apply specular intensity multiplier for user control
    float3 punctual = clamp(nl * (diffPunc + specPunc * waterSpecularIntensity), float3(0, 0, 0), float3(10, 10, 10)) * sunlit_linear * shadow * atten;

    // S24 - Apply reflection intensity multiplier and color temperature control
    radiance *= df2.y * waterReflectionIntensity;
    // S24 Advanced - Apply reflection color warmth (artistic color grading)
    radiance *= waterReflectionWarmth;

    // S24 Advanced - Apply water color tint with alpha blending for artistic control
    float3 tintedWater = fb.rgb * waterColorTint;
    float3 untintedWater = fb.rgb;
    float3 finalWater = lerp(untintedWater, tintedWater, waterColorTintAlpha);
    // With radiance forced to zero (see the task #147 note above), this
    // lerp still does something sensible: at grazing angles (high
    // df2.x/Fresnel, where a real reflection would dominate) water fades
    // toward black instead of showing garbage - a graceful, physically-
    // reasonable degradation rather than an arbitrary special case. Real
    // reflection color returns automatically once #147 lands and radiance
    // is genuinely populated - no change needed here at that point.
    float3 color = lerp(finalWater, radiance, min(1, df2.x)) + punctual.rgb;

    // S24 Advanced - Unlock hardcoded shore fade distance (was 60)
    // We shorten the fade here at the shoreline so it doesn't appear too soft from a distance.
    fade *= waterShoreFadeDistance;
    fade = min(1, fade);
    color = lerp(fb.rgb, color, fade);

    float spec = min(max(max(punctual.r, punctual.g), punctual.b), 0);

    // S24 (2026-08-09, task #148): round-2 diagnostic (paint water solid
    // yellow via fade/water_mask, no shading) removed - it did its job:
    // the yellow shape itself hovered/rocked, proving real GEOMETRY motion,
    // not a shading bug. Root cause found: DXPipeline::renderGeomPostDeferred()
    // (dxpipeline.cpp) never reset the model matrix between pools, so water
    // (the one post-deferred pool with no per-item applyModelMatrix() call)
    // inherited whatever model matrix the last-drawn alpha object left
    // behind. Fixed there - see that function's comment.
    return min(float4(1, 1, 1, 1), max(float4(color.rgb, spec * water_mask), float4(0, 0, 0, 0)));
}
