/**
 * @file class1/deferred/skyF.hlsl
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

Texture2D rainbow_map : register(t0);
SamplerState rainbow_mapSampler : register(s0);
Texture2D halo_map : register(t1);
SamplerState halo_mapSampler : register(s1);

#ifdef HAS_HDRI
Texture2D environmentMap : register(t2);
SamplerState environmentMapSampler : register(s2);
uniform float sky_hdr_scale;
uniform float hdri_split_screen;
uniform float3x3 env_mat;
#endif

uniform float moisture_level;
uniform float droplet_radius;
uniform float ice_level;

float3 srgb_to_linear(float3 c);
float3 linear_to_srgb(float3 c);

static const float PI = 3.14159265;

struct PSInput
{
    float3 vary_HazeColor : TEXCOORD0;
    float vary_LightNormPosDot : TEXCOORD1;
#ifdef HAS_HDRI
    float4 vary_position : TEXCOORD2;
    float3 vary_rel_pos : TEXCOORD3;
#endif
};

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

float3 rainbow(float d)
{
    d = clamp(-0.575 - d, 0.0, 1.0);
    float interior_coord = max(0.0, d - 0.25) * 4.2857;
    d = clamp(d, 0.0, 0.25) + interior_coord;
    float rad = (droplet_radius - 5.0f) / 1024.0f;
    return pow(rainbow_map.Sample(rainbow_mapSampler, float2(rad + 0.5, d)).rgb, float3(1.8, 1.8, 1.8)) * moisture_level;
}

float3 halo22(float d)
{
    d = clamp(d, 0.1, 1.0);
    float v = sqrt(clamp(1 - (d * d), 0, 1));
    return halo_map.Sample(halo_mapSampler, float2(0, v)).rgb * ice_level;
}

PSOutput main(PSInput IN)
{
    PSOutput OUT;
    float3 color;

#ifdef HAS_HDRI
    float3 frag_coord = IN.vary_position.xyz / IN.vary_position.w;
    if (-frag_coord.x > ((1.0 - hdri_split_screen) * 2.0 - 1.0))
    {
        float3 pos = normalize(IN.vary_rel_pos);
        pos = mul(env_mat, pos);
        float2 texCoord = float2(atan2(pos.z, pos.x) + PI, acos(pos.y)) / float2(2.0 * PI, PI);
        color = environmentMap.SampleLevel(environmentMapSampler, texCoord.xy, 0).rgb * sky_hdr_scale;
        color = min(color, float3(8192 * 8192 * 16, 8192 * 8192 * 16, 8192 * 8192 * 16));

        OUT.data2 = float4(0.0, 0.0, 0.0, GBUFFER_FLAG_HAS_HDRI);
    }
    else
#endif
    {
        color = IN.vary_HazeColor;

        float rel_pos_lightnorm = IN.vary_LightNormPosDot;
        float optic_d = rel_pos_lightnorm;
        float3 halo_22_val = halo22(optic_d);
        color.rgb += rainbow(optic_d);
        color.rgb += halo_22_val;
        color.rgb *= 2.0;
        color.rgb = clamp(color.rgb, float3(0, 0, 0), float3(5, 5, 5));

        OUT.data2 = float4(0.0, 0.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);
    }

    OUT.data1 = float4(0, 0, 0, 0);

#if defined(HAS_EMISSIVE)
    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data3 = float4(color.rgb, 1.0);
#else
    OUT.data0 = float4(color.rgb, 1.0);
#endif
    return OUT;
}
