/**
 * @file class1/deferred/cloudsF.hlsl
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

/////////////////////////////////////////////////////////////////////////
// The fragment shader for the sky
/////////////////////////////////////////////////////////////////////////

Texture2D cloud_noise_texture : register(t0);
SamplerState cloud_noise_textureSampler : register(s0);
Texture2D cloud_noise_texture_next : register(t1);
SamplerState cloud_noise_texture_nextSampler : register(s1);
uniform float blend_factor;
uniform float3 cloud_pos_density1;
uniform float3 cloud_pos_density2;
uniform float cloud_scale;
uniform float cloud_variance;

struct PSInput
{
    float3 vary_CloudColorSun : TEXCOORD0;
    float3 vary_CloudColorAmbient : TEXCOORD1;
    float vary_CloudDensity : TEXCOORD2;
    float2 vary_texcoord0 : TEXCOORD3;
    float2 vary_texcoord1 : TEXCOORD4;
    float2 vary_texcoord2 : TEXCOORD5;
    float2 vary_texcoord3 : TEXCOORD6;
    float altitude_blend_factor : TEXCOORD7;
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

float4 cloudNoise(float2 uv)
{
   float4 a = cloud_noise_texture.Sample(cloud_noise_textureSampler, uv);
   float4 b = cloud_noise_texture_next.Sample(cloud_noise_texture_nextSampler, uv);
   float4 cloud_noise_sample = lerp(a, b, blend_factor);
   return cloud_noise_sample;
}

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // Set variables
    float2 uv1 = IN.vary_texcoord0.xy;
    float2 uv2 = IN.vary_texcoord1.xy;

    float3 cloudColorSun = IN.vary_CloudColorSun;
    float3 cloudColorAmbient = IN.vary_CloudColorAmbient;
    float cloudDensity = IN.vary_CloudDensity;
    float2 uv3 = IN.vary_texcoord2.xy;
    float2 uv4 = IN.vary_texcoord3.xy;

    if (cloud_scale < 0.001)
    {
        discard;
    }

    float2 disturbance  = float2(cloudNoise(uv1 / 8.0f).x, cloudNoise((uv3 + uv1) / 16.0f).x) * cloud_variance * (1.0f - cloud_scale * 0.25f);
    float2 disturbance2 = float2(cloudNoise((uv1 + uv3) / 4.0f).x, cloudNoise((uv4 + uv2) / 8.0f).x) * cloud_variance * (1.0f - cloud_scale * 0.25f);

    // Offset texture coords
    uv1 += cloud_pos_density1.xy + (disturbance * 0.2);    //large texture, visible density
    uv2 += cloud_pos_density1.xy;   //large texture, self shadow
    uv3 += cloud_pos_density2.xy;   //small texture, visible density
    uv4 += cloud_pos_density2.xy;   //small texture, self shadow

    float density_variance = min(1.0, (disturbance.x* 2.0 + disturbance.y* 2.0 + disturbance2.x + disturbance2.y) * 4.0);

    cloudDensity *= 1.0 - (density_variance * density_variance);

    // Compute alpha1, the main cloud opacity

    float alpha1 = (cloudNoise(uv1).x - 0.5) + (cloudNoise(uv3).x - 0.5) * cloud_pos_density2.z;
    alpha1 = min(max(alpha1 + cloudDensity, 0.) * 10 * cloud_pos_density1.z, 1.);

    // And smooth
    alpha1 = 1. - alpha1 * alpha1;
    alpha1 = 1. - alpha1 * alpha1;

    alpha1 *= IN.altitude_blend_factor;
    alpha1 = clamp(alpha1, 0.0, 1.0);

    // Compute alpha2, for self shadowing effect
    // (1 - alpha2) will later be used as percentage of incoming sunlight
    float alpha2 = (cloudNoise(uv2).x - 0.5);
    alpha2 = min(max(alpha2 + cloudDensity, 0.) * 2.5 * cloud_pos_density1.z, 1.);

    // And smooth
    alpha2 = 1. - alpha2;
    alpha2 = 1. - alpha2 * alpha2;

    // Combine
    float3 color;
    color = (cloudColorSun*(1.-alpha2) + cloudColorAmbient);
    color.rgb = clamp(color.rgb, float3(0, 0, 0), float3(1, 1, 1));
    color.rgb *= 2.0;

    /// Gamma correct for WL (soft clip effect).

    OUT.data1 = float4(0.0, 0.0, 0.0, 0.0);
    OUT.data2 = float4(0, 0, 0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data3 = float4(color.rgb, alpha1);
#else
    OUT.data0 = float4(color.rgb, alpha1);
#endif

    return OUT;
}
