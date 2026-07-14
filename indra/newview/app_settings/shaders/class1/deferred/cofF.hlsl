/**
 * @file class1/deferred/cofF.hlsl
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
Texture2D depthMap : register(t1);
SamplerState depthMapSampler : register(s1);

uniform float depth_cutoff;
uniform float norm_cutoff;
uniform float focal_distance;
uniform float blur_constant;
uniform float tan_pixel_angle;
uniform float magnification;
uniform float max_cof;

uniform float4x4 inv_proj;
uniform float2 screen_res;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float calc_cof(float depth)
{
    float sc = (depth-focal_distance)/-depth*blur_constant;

    sc /= magnification;

    // tan_pixel_angle = pixel_length/-depth;
    float pixel_length =  tan_pixel_angle*-focal_distance;

    sc = sc/pixel_length;
    sc *= 1.414;

    return sc;
}

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord.xy;

    float z = depthMap.Sample(depthMapSampler, tc).r;
    z = z*2.0-1.0;
    float4 ndc = float4(0.0, 0.0, z, 1.0);
    float4 p = mul(inv_proj, ndc);
    float depth = p.z/p.w;

    float4 diff = diffuseRect.Sample(diffuseRectSampler, IN.vary_fragcoord.xy);

    float sc = calc_cof(depth);
    sc = min(sc, max_cof);
    sc = max(sc, -max_cof);

    float4 frag_color;
    frag_color.rgb = diff.rgb;
    frag_color.a = sc/max_cof*0.5+0.5;
    return frag_color;
}
