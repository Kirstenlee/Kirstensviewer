/**
 * @file class1/deferred/deferredUtil.hlsl
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


/*  Parts of this file are taken from Sascha Willem's Vulkan GLTF refernce implementation
MIT License

Copyright (c) 2018 Sascha Willems

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

uniform Texture2D normalMap : register(t0);
uniform Texture2D depthMap : register(t1);
uniform Texture2D projectionMap : register(t2);
uniform Texture2D brdfLut : register(t3);
uniform SamplerState normalMapSampler : register(s0);
uniform SamplerState depthMapSampler : register(s1);
uniform SamplerState projectionMapSampler : register(s2);
uniform SamplerState brdfLutSampler : register(s3);

// projected lighted params
uniform float4x4 proj_mat;
uniform float3 proj_n;
uniform float3 proj_p;
uniform float proj_focus;
uniform float proj_lod;
uniform float proj_range;
uniform float proj_ambiance;

uniform int classic_mode;

// light params
uniform float3 color;
uniform float size;

uniform float4x4 inv_proj;
uniform float2 screen_res;

static const float M_PI = 3.14159265;
static const float ONE_OVER_PI = 0.3183098861;

float3 srgb_to_linear(float3 cs);
float3 linear_to_srgb(float3 cs);
float3 atmosFragLightingLinear(float3 light, float3 additive, float3 atten);

float4 decodeNormal(float4 norm);

float3 clampHDRRange(float3 color)
{
    color = lerp(color, float3(1, 1, 1), isinf(color));
    color = lerp(color, float3(0, 0, 0), isnan(color));
    return clamp(color, float3(0, 0, 0), float3(11.2, 11.2, 11.2));
}

float calcLegacyDistanceAttenuation(float distance, float falloff)
{
    float dist_atten = 1.0 - clamp((distance + falloff) / (1.0 + falloff), 0.0, 1.0);
    dist_atten *= dist_atten;
    dist_atten *= 2.0;
    return dist_atten;
}

void calcHalfVectors(float3 lv, float3 n, float3 v,
    out float3 h, out float3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist)
{
    l = normalize(lv);
    h = normalize(l + v);
    float eps = 0.000001;
    nh = clamp(dot(n, h), eps, 1.0);
    nl = clamp(dot(n, l), eps, 1.0);
    nv = clamp(dot(n, v), eps, 1.0);
    vh = clamp(dot(v, h), eps, 1.0);
    lightDist = length(lv);
}

bool clipProjectedLightVars(float3 light_center, float3 pos, out float dist, out float l_dist, out float3 lv, out float4 proj_tc)
{
    lv = light_center - pos.xyz;
    dist = length(lv);
    bool clipped = (dist >= size);
    if (!clipped)
    {
        dist /= size;
        l_dist = -dot(lv, proj_n);
        float4 projected_point = mul(proj_mat, float4(pos.xyz, 1.0));
        clipped = (projected_point.z < 0.0);
        projected_point.xyz /= projected_point.w;
        proj_tc = projected_point;
    }
    return clipped;
}

float2 getScreenCoordinate(float2 screenpos)
{
    return screenpos.xy * 2.0 - float2(1.0, 1.0);
}

float4 getNorm(float2 screenpos)
{
    return decodeNormal(normalMap.Sample(normalMapSampler, screenpos.xy));
}

float4 getNormRaw(float2 screenpos)
{
    return normalMap.Sample(normalMapSampler, screenpos.xy);
}

float linearDepth(float d, float znear, float zfar)
{
    d = d * 2.0 - 1.0;
    return znear * 2.0 * zfar / (zfar + znear - d * (zfar - znear));
}

float linearDepth01(float d, float znear, float zfar)
{
    return linearDepth(d, znear, zfar) / zfar;
}

float getDepth(float2 pos_screen)
{
    return depthMap.Sample(depthMapSampler, pos_screen).r;
}

float4 getTexture2DLodAmbient(float2 tc, float lod)
{
    float4 ret = projectionMap.SampleLevel(projectionMapSampler, tc, lod);
    ret.rgb = srgb_to_linear(ret.rgb);
    float2 dist = tc - float2(0.5, 0.5);
    float d = dot(dist, dist);
    ret *= min(clamp((0.25 - d) / 0.25, 0.0, 1.0), 1.0);
    return ret;
}

float4 getTexture2DLodDiffuse(float2 tc, float lod)
{
    float4 ret = projectionMap.SampleLevel(projectionMapSampler, tc, lod);
    ret.rgb = srgb_to_linear(ret.rgb);
    float2 dist = float2(0.5, 0.5) - abs(tc - float2(0.5, 0.5));
    float det = min(lod / (proj_lod * 0.5), 1.0);
    float d = min(dist.x, dist.y);
    ret *= clamp(d / (0.25 * det), 0.0, 1.0);
    return ret;
}

float3 getProjectedLightAmbiance(float amb_da, float attenuation, float lit, float nl, float noise, float2 projected_uv)
{
    float4 amb_plcol = getTexture2DLodAmbient(projected_uv, proj_lod);
    float3 amb_rgb = amb_plcol.rgb * amb_plcol.a;
    amb_da += proj_ambiance;
    amb_da += (nl * nl * 0.5 + 0.5) * proj_ambiance;
    amb_da *= attenuation * noise;
    amb_da = min(amb_da, 1.0 - lit);
    return (amb_da * color.rgb * amb_rgb);
}

float3 getProjectedLightDiffuseColor(float light_distance, float2 projected_uv)
{
    float diff = clamp((light_distance - proj_focus) / proj_range, 0.0, 1.0);
    float lod = diff * proj_lod;
    float4 plcol = getTexture2DLodDiffuse(projected_uv.xy, lod);
    return color.rgb * plcol.rgb * plcol.a;
}

float4 texture2DLodSpecular(float2 tc, float lod)
{
    float4 ret = projectionMap.SampleLevel(projectionMapSampler, tc, lod);
    ret.rgb = srgb_to_linear(ret.rgb);
    float2 dist = float2(0.5, 0.5) - abs(tc - float2(0.5, 0.5));
    float det = min(lod / (proj_lod * 0.5), 1.0);
    float d = min(dist.x, dist.y);
    d *= min(1, d * (proj_lod - lod));
    ret *= clamp(d / (0.25 * det), 0.0, 1.0);
    return ret;
}

float3 getProjectedLightSpecularColor(float3 pos, float3 n)
{
    float3 slit = float3(0, 0, 0);
    float3 ref = reflect(normalize(pos), n);
    float3 pdelta = proj_p - pos;
    float l_dist = length(pdelta);
    float ds = dot(ref, proj_n);
    if (ds < 0.0)
    {
        float3 pfinal = pos + ref * dot(pdelta, proj_n) / ds;
        float4 stc = mul(proj_mat, float4(pfinal.xyz, 1.0));
        if (stc.z > 0.0)
        {
            stc /= stc.w;
            slit = getProjectedLightDiffuseColor(l_dist, stc.xy);
        }
    }
    return slit;
}

float3 getProjectedLightSpecularColor(float light_distance, float2 projected_uv)
{
    float diff = clamp((light_distance - proj_focus) / proj_range, 0.0, 1.0);
    float lod = diff * proj_lod;
    float4 plcol = getTexture2DLodDiffuse(projected_uv.xy, lod);
    return color.rgb * plcol.rgb * plcol.a;
}

float4 getPosition(float2 pos_screen)
{
    float depth = getDepth(pos_screen);
    float2 sc = getScreenCoordinate(pos_screen);
    float4 ndc = float4(sc.x, sc.y, 2.0 * depth - 1.0, 1.0);
    float4 pos = mul(inv_proj, ndc);
    pos /= pos.w;
    pos.w = 1.0;
    return pos;
}

float3 getPositionWithNDC(float3 ndc)
{
    float4 pos = mul(inv_proj, float4(ndc, 1.0));
    return pos.xyz / pos.w;
}

float4 getPositionWithDepth(float2 pos_screen, float depth)
{
    float2 sc = getScreenCoordinate(pos_screen);
    float3 ndc = float3(sc.x, sc.y, 2.0 * depth - 1.0);
    return float4(getPositionWithNDC(ndc), 1.0);
}

float2 getScreenCoord(float4 clip)
{
    float4 ndc = clip;
    ndc.xyz /= clip.w;
    return ndc.xy * 0.5 + 0.5;
}

float2 getScreenXY(float4 clip)
{
    float2 screen = getScreenCoord(clip);
    return screen * screen_res;
}

// Color utils

float3 colorize_dot(float x)
{
    if (x > 0.0) return float3(0, x, 0);
    if (x < 0.0) return float3(-x, 0, 0);
    return float3(0, 0, 1);
}

float3 hue_to_rgb(float hue)
{
    if (hue > 1.0) return float3(0.5, 0.5, 0.5);
    float3 rgb = abs(hue * 6. - float3(3, 2, 4)) * float3(1, -1, -1) + float3(-1, 2, 2);
    return clamp(rgb, 0.0, 1.0);
}

// PBR Utils

float2 BRDF(float NoV, float roughness)
{
    return brdfLut.Sample(brdfLutSampler, float2(NoV, roughness)).rg;
}

void pbrIbl(float3 diffuseColor,
            float3 specularColor,
            float3 radiance,
            float3 irradiance,
            float ao,
            float nv,
            float perceptualRough,
            out float3 diffuseOut,
            out float3 specularOut)
{
    float2 brdf = BRDF(clamp(nv, 0, 1), 1.0 - perceptualRough);
    float3 diffuseLight = irradiance;
    float3 specularLight = radiance;

    float3 diffuse = diffuseLight * diffuseColor;
    float3 specular = specularLight * (specularColor * brdf.x + brdf.y);

    diffuseOut = diffuse * ao;
    specularOut = specular * ao;
}

struct PBRInfo
{
    float NdotL;
    float NdotV;
    float NdotH;
    float LdotH;
    float VdotH;
    float perceptualRoughness;
    float metalness;
    float3 reflectance0;
    float3 reflectance90;
    float alphaRoughness;
    float3 diffuseColor;
    float3 specularColor;
};

float3 diffuse(PBRInfo pbrInputs)
{
    return pbrInputs.diffuseColor / M_PI;
}

float3 specularReflection(PBRInfo pbrInputs)
{
    return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

float geometricOcclusion(PBRInfo pbrInputs)
{
    float NdotL = pbrInputs.NdotL;
    float NdotV = pbrInputs.NdotV;
    float r = pbrInputs.alphaRoughness;

    float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
    float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
    return attenuationL * attenuationV;
}

float microfacetDistribution(PBRInfo pbrInputs)
{
    float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
    float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
    return roughnessSq / (M_PI * f * f);
}

void pbrPunctual(float3 diffuseColor, float3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    float3 n,
                    float3 v,
                    float3 l,
                    out float nl,
                    out float3 diff,
                    out float3 spec)
{
    perceptualRoughness = max(perceptualRoughness, 8.0 / 255.0);

    float alphaRoughness = perceptualRoughness * perceptualRoughness;

    float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

    float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
    float3 specularEnvironmentR0 = specularColor.rgb;
    float3 specularEnvironmentR90 = float3(1.0, 1.0, 1.0) * reflectance90;

    float3 h = normalize(l + v);
    float3 reflection = -normalize(reflect(v, n));
    reflection.y *= -1.0;

    float NdotL = clamp(dot(n, l), 0.001, 1.0);
    float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
    float NdotH = clamp(dot(n, h), 0.0, 1.0);
    float LdotH = clamp(dot(l, h), 0.0, 1.0);
    float VdotH = clamp(dot(v, h), 0.0, 1.0);

    PBRInfo pbrInputs = (PBRInfo)0;
    pbrInputs.NdotL = NdotL;
    pbrInputs.NdotV = NdotV;
    pbrInputs.NdotH = NdotH;
    pbrInputs.LdotH = LdotH;
    pbrInputs.VdotH = VdotH;
    pbrInputs.perceptualRoughness = perceptualRoughness;
    pbrInputs.metalness = metallic;
    pbrInputs.reflectance0 = specularEnvironmentR0;
    pbrInputs.reflectance90 = specularEnvironmentR90;
    pbrInputs.alphaRoughness = alphaRoughness;
    pbrInputs.diffuseColor = diffuseColor;
    pbrInputs.specularColor = specularColor;

    float3 F = specularReflection(pbrInputs);
    float G = geometricOcclusion(pbrInputs);
    float D = microfacetDistribution(pbrInputs);

    float3 diffuseContrib = (1.0 - F) * diffuse(pbrInputs);
    float3 specContrib = F * G * D / (4.0 * NdotL * NdotV);

    nl = NdotL;
    diff = diffuseContrib;
    spec = specContrib;
}

float3 pbrCalcPointLightOrSpotLight(float3 diffuseColor, float3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    float3 n,
                    float3 p,
                    float3 v,
                    float3 lp,
                    float3 ld,
                    float3 lightColor,
                    float lightSize, float falloff, float is_pointlight, float ambiance)
{
    float3 color = float3(0, 0, 0);

    float3 lv = lp.xyz - p;

    float lightDist = length(lv);

    float dist = lightDist / lightSize;
    if (dist <= 1.0)
    {
        lv /= lightDist;

        float dist_atten = calcLegacyDistanceAttenuation(dist, falloff);

        float spot = max(dot(-ld, lv), is_pointlight);
        float spot_atten = spot * spot;

        float3 intensity = spot_atten * dist_atten * lightColor * 3.0;

        float nl = 0;
        float3 diffPunc = float3(0, 0, 0);
        float3 specPunc = float3(0, 0, 0);

        pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, n.xyz, v, lv, nl, diffPunc, specPunc);
        color = intensity * clamp(nl * (diffPunc + specPunc), float3(0, 0, 0), float3(10, 10, 10));
    }
    float final_scale = 1.0;
    if (classic_mode > 0)
        final_scale = 0.9;
    return color * final_scale;
}

void calcDiffuseSpecular(float3 baseColor, float metallic, inout float3 diffuseColor, inout float3 specularColor)
{
    float3 f0 = float3(0.04, 0.04, 0.04);
    diffuseColor = baseColor * (float3(1.0, 1.0, 1.0) - f0);
    diffuseColor *= 1.0 - metallic;
    specularColor = lerp(f0, baseColor, metallic);
}

float3 pbrBaseLight(float3 diffuseColor, float3 specularColor, float metallic, float3 v, float3 norm, float perceptualRoughness, float3 light_dir, float3 sunlit, float scol, float3 radiance, float3 irradiance, float3 colorEmissive, float ao, float3 additive, float3 atten)
{
    float3 color = float3(0, 0, 0);

    float NdotV = clamp(abs(dot(norm, v)), 0.001, 1.0);
    float3 iblDiff = float3(0, 0, 0);
    float3 iblSpec = float3(0, 0, 0);
    pbrIbl(diffuseColor, specularColor, radiance, irradiance, ao, NdotV, perceptualRoughness, iblDiff, iblSpec);

    color += iblDiff;

    float nl = 0;
    float3 diffPunc = float3(0, 0, 0);
    float3 specPunc = float3(0, 0, 0);
    pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, norm, v, normalize(light_dir), nl, diffPunc, specPunc);

    if (classic_mode > 0)
    {
        irradiance.rgb = srgb_to_linear(irradiance * 0.9);

        float da = pow(nl, 1.2);

        float3 sun_contrib = float3(min(da, scol), min(da, scol), min(da, scol));

        sun_contrib = srgb_to_linear(linear_to_srgb(sun_contrib) * sunlit * 0.7) * M_PI;

        float3 finalAmbient = irradiance.rgb * diffuseColor.rgb;
        float3 finalSun = clamp(sun_contrib * ((diffPunc.rgb + specPunc.rgb) * scol), float3(0, 0, 0), float3(10, 10, 10));
        color.rgb = srgb_to_linear(linear_to_srgb(finalAmbient) + (linear_to_srgb(finalSun) * 1.1));
    }
    else
    {
        color += clamp(nl * (diffPunc + specPunc), float3(0, 0, 0), float3(10, 10, 10)) * sunlit * 3.0 * scol;
    }

    color.rgb += iblSpec.rgb;

    color += colorEmissive;

    return color;
}

uniform float4 waterPlane;
uniform float waterSign;

void waterClip(float3 pos)
{
    if (waterSign > 0)
    {
        if ((dot(pos.xyz, waterPlane.xyz) + waterPlane.w) < 0.0)
        {
            discard;
        }
    }
    else
    {
        if ((dot(pos.xyz, waterPlane.xyz) + waterPlane.w) > 0.0)
        {
            discard;
        }
    }
}
