/**
 * @file class1/deferred/dofCombineF.hlsl
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

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);
Texture2D lightMap : register(t1);
SamplerState lightMapSampler : register(s1);

uniform float4x4 inv_proj;
uniform float2 screen_res;

uniform float max_cof;
uniform float res_scale;
uniform float dof_width;
uniform float dof_height;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float4 dofSample(Texture2D tex, SamplerState texSampler, float2 tc)
{
    tc.x = min(tc.x, dof_width);
    tc.y = min(tc.y, dof_height);

    return tex.Sample(texSampler, tc);
}

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord.xy;

    float4 dof = dofSample(diffuseRect, diffuseRectSampler, IN.vary_fragcoord.xy*res_scale);

    float4 diff = lightMap.Sample(lightMapSampler, IN.vary_fragcoord.xy);

    float a = min(abs(diff.a*2.0-1.0) * max_cof*res_scale*res_scale, 1.0);

    if (a > 0.25 && a < 0.75)
    { //help out the transition a bit
        float sc = a/res_scale;

        float4 col;
        col = lightMap.Sample(lightMapSampler, IN.vary_fragcoord.xy+float2(sc,sc)/screen_res);
        col += lightMap.Sample(lightMapSampler, IN.vary_fragcoord.xy+float2(-sc,sc)/screen_res);
        col += lightMap.Sample(lightMapSampler, IN.vary_fragcoord.xy+float2(sc,-sc)/screen_res);
        col += lightMap.Sample(lightMapSampler, IN.vary_fragcoord.xy+float2(-sc,-sc)/screen_res);

        diff = lerp(diff, col*0.25, a);
    }

    return lerp(diff, dof, a);
}
