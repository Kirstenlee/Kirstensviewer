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

// S24: ported from class3/environment/waterF.glsl; unused forward declarations present in the
// GLSL source (scaleSoftClipFragLinear, BRDF, pbrIbl, pbrBaseLight, linear_to_srgb,
// atmosLighting, scaleSoftClip, toneMapNoExposure) were dropped since main() never calls them.
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

// S24: register map for this shader (hasReflectionProbes=true pulls in extra attached files):
// t0-t3 deferredUtil.hlsl, t5-t8 water's own textures, t9 reflectionProbeF.hlsl's environmentMap,
// t10-t15 shadowUtil. t4 is the only free slot, used by exclusionTex below.
Texture2D bumpMap : register(t5);
Texture2D bumpMap2 : register(t6);
SamplerState bumpMapSampler : register(s5);
SamplerState bumpMap2Sampler : register(s6);
uniform float blend_factor;

#ifdef TRANSPARENT_WATER
Texture2D screenTex : register(t7);
SamplerState screenTexSampler : register(s7);
// S24: do not declare a second "depthMap" texture here - deferredUtil.hlsl (attached since
// hasReflectionProbes=true) already declares one at t1, and HLSL rejects the duplicate symbol
// (unlike GL's separate-compile-then-link). LLPipeline::bindDeferredShader(shader, nullptr,
// &mWaterDis) binds mWaterDis to that exact reserved "depthMap" name (LLShaderMgr::DEFERRED_DEPTH)
// to redirect water's depth read - a texture under any other name won't receive it. Use
// deferredUtil.hlsl's getDepth() (forward-declared below) instead; it already applies the
// GL-vs-D3D11 origin flip internally.
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

uniform float waterMetallic;
uniform float waterRoughnessOverride;
uniform float waterSpecularIntensity;
uniform float waterReflectionIntensity;

uniform float3 waterColorTint;
uniform float waterColorTintAlpha;
uniform float waterFresnelPower;
uniform float waterWaveSpeed;           // Applied in C++ to phase_time
uniform float waterShoreFadeDistance;
uniform float waterUnderwaterFogMult;   // Applied in C++ to fog_density
uniform float waterReflectionWarmth;
uniform float waterColorAbsorptionRate;

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

// S24: HLSL globals are implicitly-const (X3025) unless marked static, unlike GLSL where a plain
// global is mutable. vN/vT/vB are written in main() and read back by transform_normal().
static float3 vN, vT, vB;

float3 transform_normal(float3 vNt)
{
    return normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);
}

float3 BlendNormal(float3 bump1, float3 bump2)
{
    return lerp(bump1, bump2, blend_factor);
}

// S24: Reoriented Normal Mapping (RNM) - composes a "detail" tangent-space normal onto a "base"
// one instead of averaging, which can partially cancel when two wave layers' phases drift out of
// sync (Barre-Brisebois/Hill, "Blending in Detail", blog.selfshadow.com/publications/blending-in-detail).
// Unpacked-normal fast path: both inputs are unit tangent-space normals in -1..1 with z toward viewer.
float3 RNMBlend(float3 n1, float3 n2)
{
    n1 += float3(0, 0, 1);
    n2 *= float3(-1, -1, 1);
    return n1 * dot(n1, n2) / n1.z - n2;
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

    // S24: RNM compose (see RNMBlend() above) instead of a plain weighted average. wave2/wave3 are
    // attenuated toward flat (0,0,1) by their original 0.4/0.6 weights before composing, preserving
    // the original "wave3 matters more than wave2" balance.
    float3 wavef = normalize(wave1);
    wavef = RNMBlend(wavef, normalize(lerp(float3(0, 0, 1), normalize(wave2), 0.4)));
    wavef = RNMBlend(wavef, normalize(lerp(float3(0, 0, 1), normalize(wave3), 0.6)));

    float dmod = sqrt(dist);
    // S24: getScreenCoord() does a true perspective divide by clip W - dividing refCoord.xy by
    // refCoord.z instead only works under GL's -w..w clip-space Z range and warps under D3D11's
    // 0..w range. distort stays unflipped; flip is applied at each .Sample() site instead, and
    // getPositionWithNDC()'s NDC reconstruction requires the unflipped value.
    float2 distort = getScreenCoord(IN.refCoord);

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

    // S24: wave_ibl feeds the SSR/reflection-probe ray direction and is deliberately built from a
    // COARSER normal than wavef (which still drives lighting/fresnel/specular below, untouched).
    // tapScreenSpaceReflection()'s traced ray is `reflect(viewPos, normalize(n))`, directly
    // sensitive to the input normal; wavef's full 3-layer detail changes every pixel/frame, and
    // with only a few stochastic SSR samples and no temporal accumulation that reads as flicker.
    // waveCoarse keeps mostly wave1 (slow swell) with a little wave2/wave3 blended in via the same
    // RNM technique, stabilizing the ray direction without softening wavef's own visible detail.
    float3 waveCoarse = normalize(wave1);
    waveCoarse = RNMBlend(waveCoarse, normalize(lerp(float3(0, 0, 1), normalize(wave2), 0.15)));
    waveCoarse = RNMBlend(waveCoarse, normalize(lerp(float3(0, 0, 1), normalize(wave3), 0.15)));

    float3 wave_ibl = waveCoarse * normScale;
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

    // S24: exclusionTex (mWaterExclusionMask) is a real D3D11 render target - flip at this sample
    // site only; distort itself stays unflipped since it also feeds sampleDirectionalShadow()/
    // reflection probes below.
    float water_mask = exclusionTex.Sample(exclusionTexSampler, float2(distort.x, 1.0 - distort.y)).r;

#ifdef HAS_SUN_SHADOW
    shadow = sampleDirectionalShadow(pos.xyz, norm.xyz, distort);
#endif

    float3 sunlit_linear = sunlit;
    float fade = 1;
#ifdef TRANSPARENT_WATER
    // S24: getDepth() reads mWaterDis (bound via bindDeferredShader(shader, nullptr, &mWaterDis) -
    // see this file's header comment) and applies the origin flip internally.
    float depth = getDepth(distort);

    // S24: 1.0-2.0*depth (reversed-Z), not 2.0*depth-1.0 - see deferredUtil.hlsl's linearDepth().
    // Precision loss from the non-reversed form at range caused the shore-fade heuristic to
    // misjudge deep water as shallow.
    float3 refPos = getPositionWithNDC(float3(distort * 2.0 - float2(1.0, 1.0), 1.0 - 2.0 * depth));

    // S24: depth-under-surface at this pixel, feeds the Beer-Lambert-style color absorption below.
    // (pos.z - refPos.z) alone is a VIEW-SPACE Z delta along the camera ray, not a true vertical
    // depth - at normal eye-level viewing angles (looking across the water toward the horizon
    // rather than straight down) the ray travels almost parallel to the surface, so this delta is
    // dominated by horizontal travel distance and stays huge/saturated almost everywhere, which is
    // why an earlier version of this looked like it "did nothing" except right at the shoreline.
    // Multiplying by vdu (already computed above - how much the view ray points downward into the
    // water, 0=grazing/horizontal, 1=straight down) converts that into a real vertical-depth
    // approximation instead: the same real depth read at a steep angle gives a small Z delta and a
    // shallow (near-horizontal) angle gives a huge one, so dividing back out by "how steep" the ray
    // is recovers the actual vertical distance regardless of camera angle. The 0.05 floor keeps a
    // little absorption alive at pure-grazing angles rather than forcing it fully off.
    float waterDepth = max(0.0, pos.z - refPos.z) * max(vdu, 0.05);

    // Calculate some distance fade in the water to better assist with refraction blending and reducing the refraction texture's "disconnect".
    fade = max(0, min(1, (pos.z - refPos.z) / 10));

    fade *= water_mask;
    distort2 = lerp(distort, distort2, min(1, fade * 10));
    depth = getDepth(distort2);

    // S24: reversed-Z, see matching comment above.
    refPos = getPositionWithNDC(float3(distort2 * 2.0 - float2(1.0, 1.0), 1.0 - 2.0 * depth));

    if (pos.z < refPos.z - 0.05)
    {
        distort2 = distort;
    }

    float4 fb = screenTex.Sample(screenTexSampler, float2(distort2.x, 1.0 - distort2.y));

#else
    float4 fb = applyWaterFogViewLinear(viewVec * 2048.0, float4(1.0, 1.0, 1.0, 1.0));

    if (water_mask < 1)
        discard;

    // S24: no screen-space refraction data available in this (Transparent Water OFF) path, so no
    // real per-pixel depth to base absorption on - treat as always-deep so the tint behaves exactly
    // as it did before this feature existed.
    float waterDepth = 1000.0;
#endif

    float metallic = waterMetallic; // Was: 1.0 HARDCODED
    float perceptualRoughness = waterRoughnessOverride > 0.0 ? waterRoughnessOverride : blurMultiplier;
    float gloss = 1 - perceptualRoughness;

    float3 irradiance = float3(0, 0, 0);
    float3 radiance = float3(0, 0, 0);
    // S24: LLPipeline::bindDeferredShader() force-binds the legacy single-cubemap environmentMap/t9
    // for any shader with mFeatures.hasReflectionProbes, which gWaterProgram has; sampled by
    // sampleReflectionProbesWater() -> sampleReflectionProbes() (reflectionProbeF.hlsl). This gives
    // a real but non-per-position (single static sky cubemap) reflection, not flat black; per-probe
    // accuracy needs the LLCubeMapArray-based per-probe pipeline (llreflectionmapmanager.cpp).
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

    float3 punctual = clamp(nl * (diffPunc + specPunc * waterSpecularIntensity), float3(0, 0, 0), float3(10, 10, 10)) * sunlit_linear * shadow * atten;

    radiance *= df2.y * waterReflectionIntensity;
    radiance *= waterReflectionWarmth;

    // S24: depth-based (Beer-Lambert-style) tint ramp - shallow/shoreline water shows more of the
    // real refracted seafloor color instead of a flat uniform tint, deep water ramps up to the
    // full waterColorTintAlpha strength. Previously this was a single flat blend regardless of
    // depth, which is a big part of why SL water reads as an artificial, uniformly-tinted
    // "plastic" surface even right at the shore where real water is nearly clear.
    float depthAbsorb = 1.0 - exp(-waterDepth * waterColorAbsorptionRate);
    float3 tintedWater = fb.rgb * waterColorTint;
    float3 untintedWater = fb.rgb;
    float3 finalWater = lerp(untintedWater, tintedWater, waterColorTintAlpha * depthAbsorb);
    // At grazing angles (high df2.x/Fresnel, where reflection dominates)
    // this lerp still degrades gracefully toward black if radiance is ever
    // weak/unpopulated, rather than showing garbage.
    float3 color = lerp(finalWater, radiance, min(1, df2.x)) + punctual.rgb;

    // We shorten the fade here at the shoreline so it doesn't appear too soft from a distance.
    fade *= waterShoreFadeDistance;
    fade = min(1, fade);
    color = lerp(fb.rgb, color, fade);

    // S24: was min(..., 0) - punctual is clamped non-negative above (line 356), so max(...) was
    // always >=0 and min(that, 0) always collapsed to exactly 0, permanently zeroing this alpha
    // output. Swapped min->max (a floor, matching the clamp idiom just below) to actually pass
    // the punctual specular magnitude through.
    float spec = max(max(max(punctual.r, punctual.g), punctual.b), 0);

    // S24: water is the one post-deferred pool with no per-item applyModelMatrix() call, so it
    // depends on DXPipeline::renderGeomPostDeferred() (dxpipeline.cpp) resetting the model matrix
    // between pools - otherwise it inherits whatever matrix the previously-drawn pool left behind.
    return min(float4(1, 1, 1, 1), max(float4(color.rgb, spec * water_mask), float4(0, 0, 0, 0)));
}
