/**
 * @file class3/deferred/spotLightF.hlsl
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

TextureCube environmentMap : register(t0);
SamplerState environmentMapSampler : register(s0);
Texture2D lightMap : register(t1);
SamplerState lightMapSampler : register(s1);
Texture2D lightFunc : register(t2);
SamplerState lightFuncSampler : register(s2);

uniform float4x4 proj_mat; //screen space to light space
uniform float proj_near; //near clip for projection
uniform float3 proj_p; //plane projection is emitting from (in screen space)
uniform float3 proj_n;
uniform float proj_focus; //distance from plane to begin blurring
uniform float proj_lod;  //(number of mips in proj map)
uniform float proj_range; //range between near clip and far clip plane of projection
uniform float proj_ambient_lod;
uniform float proj_ambiance;
uniform float near_clip;
uniform float far_clip;

uniform float3 proj_origin; //origin of projection to be used for angular attenuation
uniform float sun_wash;
uniform int proj_shadow_idx;
uniform float shadow_fade;
uniform int classic_mode;

// Light params
#if defined(MULTI_SPOTLIGHT)
uniform float3 center;
#endif
uniform float size;
uniform float3 color;
uniform float falloff;

struct PSInput
{
    float4 vary_fragcoord : TEXCOORD0;
#if !defined(MULTI_SPOTLIGHT)
    float3 trans_center : TEXCOORD1;
#endif
};

uniform float2 screen_res;

uniform float4x4 inv_proj;

void calcHalfVectors(float3 lv, float3 n, float3 v, out float3 h, out float3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);
float calcLegacyDistanceAttenuation(float distance, float falloff);
bool clipProjectedLightVars(float3 center, float3 pos, out float dist, out float l_dist, out float3 lv, out float4 proj_tc );
float4 getNorm(float2 screenpos);
float3 getProjectedLightAmbiance(float amb_da, float attenuation, float lit, float nl, float noise, float2 projected_uv);
float3 getProjectedLightDiffuseColor(float light_distance, float2 projected_uv );
float2 getScreenCoord(float4 clip);
float3 srgb_to_linear(float3 cs);
float4 texture2DLodSpecular(float2 tc, float lod);

float4 getPosition(float2 pos_screen);

static const float M_PI = 3.14159265;

void pbrPunctual(float3 diffuseColor, float3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    float3 n, // normal
                    float3 v, // surface point to camera
                    float3 l, // surface point to light
                    out float nl,
                    out float3 diff,
                    out float3 spec);

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

float4 main(PSInput IN) : SV_Target
{
    float4 frag_color;

    float3 final_color = float3(0, 0, 0);
    float2 tc          = getScreenCoord(IN.vary_fragcoord);
    float3 pos         = getPosition(tc).xyz;

    float3 lv;
    float4 proj_tc;
    float dist, l_dist;
    float3 c;
#if defined(MULTI_SPOTLIGHT)
    c = center;
#else
    c = IN.trans_center;
#endif

    if (clipProjectedLightVars(c, pos, dist, l_dist, lv, proj_tc))
    {
        discard;
    }

    float shadow = 1.0;

    if (proj_shadow_idx >= 0)
    {
        float4 shd = lightMap.Sample(lightMapSampler, tc);
        shadow = (proj_shadow_idx==0)?shd.b:shd.a;
        shadow += shadow_fade;
        shadow = clamp(shadow, 0.0, 1.0);
    }

    GBufferInfo gb = getGBuffer(tc);

    float3 n = gb.normal;

    float dist_atten = calcLegacyDistanceAttenuation(dist, falloff);
    if (dist_atten <= 0.0)
    {
        discard;
    }

    lv = proj_origin-pos.xyz;
    float3  h, l, v = -normalize(pos);
    float nh, nl, nv, vh, lightDist;
    calcHalfVectors(lv, n, v, h, l, nh, nl, nv, vh, lightDist);

    float3 diffuse = gb.albedo.rgb;
    float4 spec    = gb.specular;
    float3 dlit    = float3(0, 0, 0);
    float3 slit    = float3(0, 0, 0);

    float3 amb_rgb = float3(0, 0, 0);

    if (GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_PBR))
    {
        float3 orm = spec.rgb;
        float perceptualRoughness = orm.g;
        float metallic = orm.b;
        float3 f0 = float3(0.04, 0.04, 0.04);
        float3 baseColor = diffuse.rgb;

        float3 diffuseColor = baseColor.rgb*(float3(1.0, 1.0, 1.0)-f0);
        diffuseColor *= 1.0 - metallic;

        float3 specularColor = lerp(f0, baseColor.rgb, metallic);
        float3 diffPunc = float3(0, 0, 0);
        float3 specPunc = float3(0, 0, 0);

        // We need this additional test inside a light's frustum since a spotlight's ambiance can be applied
        if (proj_tc.x > 0.0 && proj_tc.x < 1.0
        &&  proj_tc.y > 0.0 && proj_tc.y < 1.0)
        {
            float lit = 0.0;
            float amb_da = 0.0;

            lv = normalize(lv);

            if (nl > 0.0)
            {
                amb_da += (nl*0.5 + 0.5) * proj_ambiance;

                dlit = getProjectedLightDiffuseColor( l_dist, proj_tc.xy );

                float3 intensity = dist_atten * dlit * 3.25 * shadow; // Legacy attenuation, magic number to balance with legacy materials

                pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, n.xyz, v, normalize(lv), nl, diffPunc, specPunc);

                final_color += intensity * clamp(nl * (diffPunc + specPunc), float3(0, 0, 0), float3(10, 10, 10));
            }

            amb_rgb = getProjectedLightAmbiance( amb_da, dist_atten, lit, nl, 1.0, proj_tc.xy ) * 3.25; //magic number to balance with legacy ambiance
            pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, n.xyz, v, normalize(lv), nl, diffPunc, specPunc);

            final_color += amb_rgb * clamp(nl * (diffPunc + specPunc), float3(0, 0, 0), float3(10, 10, 10));
        }
    }
    else
    {
        float envIntensity = gb.envIntensity;

        diffuse = srgb_to_linear(diffuse);
        spec.rgb = srgb_to_linear(spec.rgb);

        if (proj_tc.z > 0.0 &&
            proj_tc.x < 1.0 &&
            proj_tc.y < 1.0 &&
            proj_tc.x > 0.0 &&
            proj_tc.y > 0.0)
        {
            float amb_da = 0;
            float lit = 0.0;

            if (nl > 0.0)
            {
                lit = nl * dist_atten;

                dlit = getProjectedLightDiffuseColor( l_dist, proj_tc.xy );

                final_color = dlit*lit*diffuse*shadow;

                // unshadowed for consistency between forward and deferred?
                amb_da += (nl*0.5+0.5) /* * (1.0-shadow) */ * proj_ambiance;
            }

            amb_rgb = getProjectedLightAmbiance( amb_da, dist_atten, lit, nl, 1.0, proj_tc.xy );
            final_color += diffuse.rgb * amb_rgb * max(dot(-normalize(lv), n), 0.0);
        }

        if (spec.a > 0.0)
        {
            dlit *= min(nl*6.0, 1.0) * dist_atten;

            float fres = pow(1 - vh, 5)*0.4+0.5;

            float gtdenom = 2 * nh;
            float gt = max(0, min(gtdenom * nv / vh, gtdenom * nl / vh));

            if (nh > 0.0)
            {
                float scol = fres*lightFunc.Sample(lightFuncSampler, float2(nh, spec.a)).r*gt/(nh*nl);
                float3 speccol = dlit*scol*spec.rgb*shadow;
                speccol = clamp(speccol, float3(0, 0, 0), float3(1, 1, 1));
                final_color += speccol;
            }
        }

        if (envIntensity > 0.0)
        {
            float3 ref = reflect(normalize(pos), n);

            //project from point pos in direction ref to plane proj_p, proj_n
            float3 pdelta = proj_p-pos;
            float ds = dot(ref, proj_n);

            if (ds < 0.0)
            {
                float3 pfinal = pos + ref * dot(pdelta, proj_n)/ds;

                float4 stc = mul(proj_mat, float4(pfinal.xyz, 1.0));

                if (stc.z > 0.0)
                {
                    stc /= stc.w;

                    if (stc.x < 1.0 &&
                        stc.y < 1.0 &&
                        stc.x > 0.0 &&
                        stc.y > 0.0)
                    {
                        final_color += color.rgb * texture2DLodSpecular(stc.xy, (1 - spec.a) * (proj_lod * 0.6)).rgb * shadow * envIntensity;
                    }
                }
            }
        }
    }

    //not sure why, but this line prevents MATBUG-194
    final_color = max(final_color, float3(0.0, 0.0, 0.0));
    float final_scale = 1.0;
    if (classic_mode > 0)
        final_scale = 0.9;
    //output linear
    frag_color.rgb = final_color * final_scale;
    frag_color.a = 0.0;

    return frag_color;
}
