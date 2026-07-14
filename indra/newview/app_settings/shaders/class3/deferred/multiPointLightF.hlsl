/**
 * @file class3/deferred/multiPointLightF.hlsl
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

uniform float3 env_mat[3];
uniform float sun_wash;
uniform int light_count;
uniform float4 light[LIGHT_COUNT];
uniform float4 light_col[LIGHT_COUNT];
uniform float2 screen_res;
uniform float far_z;
uniform float4x4 inv_proj;
uniform int classic_mode;

float calcLegacyDistanceAttenuation(float distance, float falloff_val);
float4 getPosition(float2 pos_screen);
float2 getScreenCoord(float4 clip);
float3 srgb_to_linear(float3 c);
GBufferInfo getGBuffer(float2 screenpos);

#include "deferredUtil.hlsl"

struct PSInput
{
    float4 vary_fragcoord : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    float3 final_color = float3(0, 0, 0);
    float2 tc = getScreenCoord(IN.vary_fragcoord);
    float3 pos = getPosition(tc).xyz;
    if (pos.z < far_z) discard;

    GBufferInfo gb = getGBuffer(tc);
    float3 n = gb.normal;
    float4 spec = gb.specular;
    float3 diffuse = gb.albedo.rgb;
    float3 v = -normalize(pos);

    if (GET_GBUFFER_FLAG(gb.gbufferFlag, GBUFFER_FLAG_HAS_PBR))
    {
        float3 orm = spec.rgb;
        float perceptualRoughness = orm.g;
        float metallic = orm.b;
        float3 f0 = 0.04;
        float3 baseColor = diffuse.rgb;
        float3 diffuseColor = baseColor.rgb * (1.0 - metallic);
        float3 specularColor = lerp(f0, baseColor.rgb, metallic);

        for (int light_idx = 0; light_idx < LIGHT_COUNT; ++light_idx)
        {
            float3 lightColor = light_col[light_idx].rgb;
            float falloff_val = light_col[light_idx].a;
            float lightSize = light[light_idx].w;
            float3 lv = light[light_idx].xyz - pos;
            float lightDist = length(lv);
            float dist = lightDist / lightSize;
            if (dist <= 1.0)
            {
                lv /= lightDist;
                float dist_atten = calcLegacyDistanceAttenuation(dist, falloff_val);
                float3 intensity = dist_atten * lightColor * 3.25;
                float nl2 = 0; float3 diffPunc, specPunc;
                pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, n.xyz, v, lv, nl2, diffPunc, specPunc);
                final_color += intensity * clamp(nl2 * (diffPunc + specPunc), 0.0, 10.0);
            }
        }
    }
    else
    {
        diffuse = srgb_to_linear(diffuse);
        spec.rgb = srgb_to_linear(spec.rgb);
        for (int i = 0; i < LIGHT_COUNT; ++i)
        {
            float3 lv = light[i].xyz - pos;
            float dist = length(lv) / light[i].w;
            if (dist <= 1.0)
            {
                float nl2 = dot(n, lv / length(lv));
                if (nl2 > 0.0)
                {
                    float3 h, l; float nh, nv, vh, _lightDist;
                    calcHalfVectors(lv, n, v, h, l, nh, nl2, nv, vh, _lightDist);
                    float fa = light_col[i].a;
                    float dist_atten = calcLegacyDistanceAttenuation(dist, fa);
                    float lit = nl2 * dist_atten;
                    float3 col = light_col[i].rgb * lit * diffuse;
                    if (spec.a > 0.0)
                    {
                        lit = min(nl2 * 6.0, 1.0) * dist_atten;
                        float fres = pow(1 - vh, 5) * 0.4 + 0.5;
                        float gt = max(0, min(2 * nh * nv / vh, 2 * nh * nl2 / vh));
                        if (nh > 0.0)
                        {
                            float scol = fres * lightFunc.Sample(lightFuncSampler, float2(nh, spec.a)).r * gt / (nh * nl2);
                            col += lit * scol * light_col[i].rgb * spec.rgb;
                        }
                    }
                    final_color += col;
                }
            }
        }
    }
    float final_scale = (classic_mode > 0) ? 0.9 : 1.0;
    return float4(max(final_color * final_scale, 0.0), 0.0);
}
