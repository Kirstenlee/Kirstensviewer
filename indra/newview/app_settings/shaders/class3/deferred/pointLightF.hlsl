/**
 * @file class3/deferred/pointLightF.hlsl
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

// t0-t3/s0-s3 reserved by deferredUtil.hlsl, t4-t6/s4-s6 by
// gbufferUtil.hlsl (moved there - see its own comment, it used to
// collide with deferredUtil.hlsl at t0-t2 until this was found),
// t10-t15/s10-s15 by shadowUtil.hlsl - t7/s7 confirmed free.
Texture2D lightFunc : register(t7);
SamplerState lightFuncSampler : register(s7);

// color/size are also declared by deferredUtil.hlsl ("light params",
// used there by its own projected-light helper functions) - genuinely the
// same "current light's color/size" uniform, set once per draw regardless
// of which light-type helper ends up reading it, not a rename candidate
// since C++ likely binds this uniform by this exact name. Include-guarded,
// same reasoning as classic_mode.
#ifndef LL_LIGHT_COLOR_SIZE_DECLARED
#define LL_LIGHT_COLOR_SIZE_DECLARED
uniform float3 color;
uniform float size;
#endif
uniform float falloff;

// screen_res/inv_proj are also declared by deferredUtil.hlsl, grouped
// together there under the same guard - reuse it here.
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float2 screen_res;
uniform float4x4 inv_proj;
#endif
uniform float4 viewport;

// classic_mode is also declared by deferredUtil.hlsl/atmosphericsFuncs.hlsl
// (independent attach conditions, neither implies the other) - same
// include-guard reasoning as every other classic_mode fix this session.
#ifndef LL_CLASSIC_MODE_DECLARED
#define LL_CLASSIC_MODE_DECLARED
uniform int classic_mode;
#endif

void calcHalfVectors(float3 lv, float3 n, float3 v, out float3 h, out float3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);
float calcLegacyDistanceAttenuation(float distance, float falloff_val);
float4 getNorm(float2 screenpos);
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

// GBufferInfo is defined for real in gbufferUtil.hlsl (attached AFTER this
// entry file's own text, per the fixed concatenation order), but this
// file's own forward declaration below needs the type visible already -
// same "entry file needs its own guarded copy" shape as PBRMix/TerrainMix.
// A stray raw `#include "deferredUtil.hlsl"` used to sit here as a
// (broken - D3DCompile has no include handler, and it named the wrong
// file besides) attempt to solve exactly this; removed, this is the real
// fix.
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

#include "varying/pointLightVarying.hlsli"

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input.
struct PSInput
{
    float4 position : SV_Position;
    PointLightVarying varying;
};

float4 main(PSInput IN) : SV_Target
{
    float3 final_color = float3(0, 0, 0);
    float2 tc = getScreenCoord(IN.varying.vary_fragcoord);
    float3 pos = getPosition(tc).xyz;
    GBufferInfo gb = getGBuffer(tc);
    float3 n = gb.normal;
    float3 diffuse = gb.albedo.rgb;
    float4 spec = gb.specular;
    float3 lv = IN.varying.trans_center.xyz - pos;
    float3 h, l, v = -normalize(pos);
    float nh, nl, nv, vh, lightDist;
    calcHalfVectors(lv, n, v, h, l, nh, nl, nv, vh, lightDist);
    if (lightDist >= size) discard;
    float dist = lightDist / size;
    float dist_atten = calcLegacyDistanceAttenuation(dist, falloff);

    if (GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_PBR))
    {
        float3 orm = spec.rgb;
        float perceptualRoughness = orm.g;
        float metallic = orm.b;
        float3 f0 = float3(0.04, 0.04, 0.04);
        float3 baseColor = diffuse.rgb;
        // S24 (2026-08-19, task #235, task #227 audit finding): was missing
        // the *(1.0-f0) term GLSL has (~4% too bright) - sibling
        // spotLightF.hlsl already has it correctly, confirming this was a
        // real, inconsistent omission.
        float3 diffuseColor = baseColor.rgb * (float3(1.0, 1.0, 1.0) - f0);
        diffuseColor *= 1.0 - metallic;
        float3 specularColor = lerp(f0, baseColor.rgb, metallic);
        float3 intensity = dist_atten * color * 3.25;
        float nl2 = 0; float3 diffPunc, specPunc;
        pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, n.xyz, v, normalize(lv), nl2, diffPunc, specPunc);
        final_color += intensity * clamp(nl2 * (diffPunc + specPunc), 0.0, 10.0);
    }
    else
    {
        // S24 (2026-08-17, task #165): dead code, left as documentation
        // rather than removed - calcHalfVectors() above (called
        // unconditionally, before the PBR/legacy branch) already clamps nl
        // to [1e-6, 1.0], so `nl < 0.0` can never be true here. This box-
        // mesh path never had the hard N.L popping bug multiPointLightF.hlsl
        // did (see that file's real fix+explanation) - it's already
        // continuous by construction, just via an accidental side effect of
        // the clamp rather than deliberate design. No behavior change.
        if (nl < 0.0) discard;
        diffuse = srgb_to_linear(diffuse);
        spec.rgb = srgb_to_linear(spec.rgb);
        float lit = nl * dist_atten;
        final_color = color.rgb * lit * diffuse;
        if (spec.a > 0.0)
        {
            lit = min(nl * 6.0, 1.0) * dist_atten;
            float fres = pow(abs(1 - vh), 5) * 0.4 + 0.5;
            float gtdenom = 2 * nh;
            float gt = max(0, min(gtdenom * nv / vh, gtdenom * nl / vh));
            if (nh > 0.0)
            {
                float scol = fres * lightFunc.Sample(lightFuncSampler, float2(nh, spec.a)).r * gt / (nh * nl);
                final_color += lit * scol * color.rgb * spec.rgb;
            }
        }
        if (dot(final_color, final_color) <= 0.0) discard;
    }
    float final_scale = (classic_mode > 0) ? 0.9 : 1.0;
    return float4(max(final_color * final_scale, 0.0), 0.0);
}
