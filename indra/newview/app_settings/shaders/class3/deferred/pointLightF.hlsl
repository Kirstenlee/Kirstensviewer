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

uniform float3 color;
uniform float falloff;
uniform float size;
uniform float2 screen_res;
uniform float4x4 inv_proj;
uniform float4 viewport;
uniform int classic_mode;

void calcHalfVectors(float3 lv, float3 n, float3 v, out float3 h, out float3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);
float calcLegacyDistanceAttenuation(float distance, float falloff_val);
float4 getNorm(float2 screenpos);
float4 getPosition(float2 pos_screen);
float2 getScreenCoord(float4 clip);
float3 srgb_to_linear(float3 c);
GBufferInfo getGBuffer(float2 screenpos);

#include "deferredUtil.hlsl"

struct PSInput
{
    float4 vary_fragcoord : TEXCOORD0;
    float3 trans_center : TEXCOORD1;
};

float4 main(PSInput IN) : SV_Target
{
    float3 final_color = float3(0, 0, 0);
    float2 tc = getScreenCoord(IN.vary_fragcoord);
    float3 pos = getPosition(tc).xyz;
    GBufferInfo gb = getGBuffer(tc);
    float3 n = gb.normal;
    float3 diffuse = gb.albedo.rgb;
    float4 spec = gb.specular;
    float3 lv = IN.trans_center.xyz - pos;
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
        float3 diffuseColor = baseColor.rgb * (1.0 - metallic);
        float3 specularColor = lerp(f0, baseColor.rgb, metallic);
        float3 intensity = dist_atten * color * 3.25;
        float nl2 = 0; float3 diffPunc, specPunc;
        pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, n.xyz, v, normalize(lv), nl2, diffPunc, specPunc);
        final_color += intensity * clamp(nl2 * (diffPunc + specPunc), 0.0, 10.0);
    }
    else
    {
        if (nl < 0.0) discard;
        diffuse = srgb_to_linear(diffuse);
        spec.rgb = srgb_to_linear(spec.rgb);
        float lit = nl * dist_atten;
        final_color = color.rgb * lit * diffuse;
        if (spec.a > 0.0)
        {
            lit = min(nl * 6.0, 1.0) * dist_atten;
            float fres = pow(1 - vh, 5) * 0.4 + 0.5;
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
