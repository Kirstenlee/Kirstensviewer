/**
 * @file class3/deferred/fullbrightShinyF.hlsl
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

#ifndef HAS_DIFFUSE_LOOKUP
Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
#endif

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD0;
    float3 vary_texcoord1 : TEXCOORD1;
    float3 vary_position : TEXCOORD2;
#ifdef HAS_DIFFUSE_LOOKUP
    nointerpolation int vary_texture_index : VARYTEXTUREINDEX;
#endif
};

// environmentMap is also declared (and actually used, via applyLegacyEnv())
// by reflectionProbeF.hlsl - this copy is dead in both the original GLSL
// and this port (declared, never sampled - the real env-map read happens
// inside applyLegacyEnv() below, against reflectionProbeF.hlsl's own copy).
// Confirmed via grep of both fullbrightShinyF.glsl and this file. Deleted,
// not guarded/renamed.

float3 atmosFragLighting(float3 light, float3 additive, float3 atten);
float4 applyWaterFogViewLinear(float3 pos, float4 color);

void calcAtmosphericVars(float3 inPositionEye, float3 light_dir, float ambFactor, out float3 sunlit, out float3 amblit, out float3 additive, out float3 atten);

float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

// reflection probe interface
void sampleReflectionProbesLegacy(inout float3 ambenv, inout float3 glossenv, inout float3 legacyenv,
        float2 tc, float3 pos, float3 norm, float glossiness, float envIntensity, bool transparent, float3 amblit_linear);

void applyLegacyEnv(inout float3 color, float3 legacyenv, float4 spec, float3 pos, float3 norm, float envIntensity);

void mirrorClip(float3 pos);

float4 main(PSInput IN) : SV_Target
{
#ifdef HAS_DIFFUSE_LOOKUP
    vary_texture_index = IN.vary_texture_index;
#endif

    mirrorClip(IN.vary_position);
#ifdef HAS_DIFFUSE_LOOKUP
    float4 color = diffuseLookup(IN.vary_texcoord0.xy);
#else
    float4 color = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);
#endif

    color.rgb *= IN.vertex_color.rgb;

    // SL-9632 HUDs are affected by Atmosphere
#ifndef IS_HUD

    float3 sunlit;
    float3 amblit;
    float3 additive;
    float3 atten;
    float3 pos = IN.vary_position;
    calcAtmosphericVars(pos.xyz, float3(0, 0, 0), 1.0, sunlit, amblit, additive, atten);

    float env_intensity = IN.vertex_color.a;

    float3 ambenv;
    // S24 (2026-09-02): both zero-initialized - D3DCompile flagged X4000
    // "potentially uninitialized variable" for legacyenv here. Real risk,
    // not a false alarm: sampleReflectionProbesLegacy()/its glossiness
    // branch only write these when envIntensity>0.0/spec.a>0.0
    // respectively - with env_intensity==0 (routine; a FullbrightShiny
    // material with no explicit Environment Intensity set), legacyenv
    // stays whatever garbage was in this register and flows straight into
    // applyLegacyEnv()'s math. That math IS designed to cancel out at
    // envIntensity==0 (reflected_color *= envIntensity, then
    // lerp(color, reflected_color*0.5, envIntensity)) - but only for
    // finite garbage; NaN/Inf survive a zero-weight lerp in IEEE float
    // (0*NaN=NaN, not 0), which uninitialized memory is not guaranteed to
    // avoid. softenLightF.hlsl/alphaF.hlsl's own callers already
    // correctly zero-init - this one just didn't.
    float3 glossenv = float3(0, 0, 0);
    float3 legacyenv = float3(0, 0, 0);
    float3 norm = normalize(IN.vary_texcoord1.xyz);
    float4 spec = float4(0, 0, 0, 0);
    sampleReflectionProbesLegacy(ambenv, glossenv, legacyenv, float2(0, 0), pos.xyz, norm.xyz, spec.a, env_intensity, false, amblit);

    color.rgb = srgb_to_linear(color.rgb);

    applyLegacyEnv(color.rgb, legacyenv, spec, pos, norm, env_intensity);
#endif

    color.a = 1.0;

    return max(color, float4(0, 0, 0, 0));
}
