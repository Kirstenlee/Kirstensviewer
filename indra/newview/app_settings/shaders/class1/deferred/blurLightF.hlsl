/**
 * @file class1/deferred/blurLightF.hlsl
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

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

uniform Texture2D lightMap : register(t0);
uniform SamplerState lightMapSampler : register(s0);

uniform float dist_factor;
uniform float blur_size;
uniform float2 delta;
uniform float2 screen_res;
uniform float3 kern[4];
uniform float kern_scale;

float4 getPosition(float2 pos_screen);
float4 getNorm(float2 pos_screen);

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord.xy;
    float4 norm = getNorm(tc);
    float3 pos = getPosition(tc).xyz;
    float4 ccol = lightMap.Sample(lightMapSampler, tc).rgba;

    float2 dlt = kern_scale * delta / (1.0 + norm.xy * norm.xy);
    dlt /= max(-pos.z * dist_factor, 1.0);

    float2 defined_weight = kern[0].xy;
    float4 col = float4(defined_weight.xyxx * ccol);

    float pointplanedist_tolerance_pow2 = pos.z * pos.z * 0.00005;

    tc *= screen_res;
    float tc_mod = 0.5 * (tc.x + tc.y);
    tc_mod -= floor(tc_mod);
    tc_mod *= 2.0;
    tc += ((tc_mod - 0.5) * kern[1].z * dlt * 0.5);

    float3 k[7];
    k[0] = kern[0];
    k[2] = kern[1];
    k[4] = kern[2];
    k[6] = kern[3];
    k[1] = (k[0] + k[2]) * 0.5f;
    k[3] = (k[2] + k[4]) * 0.5f;
    k[5] = (k[4] + k[6]) * 0.5f;

    for (int i = 1; i < 7; i++)
    {
        float2 samptc = tc + k[i].z * dlt * 2.0;
        samptc /= screen_res;
        float3 samppos = getPosition(samptc).xyz;
        float d = dot(norm.xyz, samppos.xyz - pos.xyz);
        if (d * d <= pointplanedist_tolerance_pow2)
        {
            col += lightMap.Sample(lightMapSampler, samptc) * k[i].xyxx;
            defined_weight += k[i].xy;
        }
    }

    for (int i = 1; i < 7; i++)
    {
        float2 samptc = tc - k[i].z * dlt * 2.0;
        samptc /= screen_res;
        float3 samppos = getPosition(samptc).xyz;
        float d = dot(norm.xyz, samppos.xyz - pos.xyz);
        if (d * d <= pointplanedist_tolerance_pow2)
        {
            col += lightMap.Sample(lightMapSampler, samptc) * k[i].xyxx;
            defined_weight += k[i].xy;
        }
    }

    col /= float4(defined_weight.xyxx);
    return max(col, float4(0, 0, 0, 0));
}
