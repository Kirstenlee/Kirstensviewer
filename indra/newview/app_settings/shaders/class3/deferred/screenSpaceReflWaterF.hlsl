/**
 * @file class3/deferred/screenSpaceReflWaterF.hlsl
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

Texture2D bumpMap : register(t0);
SamplerState bumpMapSampler : register(s0);
Texture2D bumpMap2 : register(t1);
SamplerState bumpMap2Sampler : register(s1);
uniform float     blend_factor;
uniform float3      normScale;
uniform float     blurMultiplier;
uniform float2      screen_res;
uniform float4x4      projection_matrix;

struct PSInput
{
    float4 refCoord : TEXCOORD0;
    float4 littleWave : TEXCOORD1;
    float4 view : TEXCOORD2;
    float3 vary_position : TEXCOORD3;
    float3 vary_normal : TEXCOORD4;
    float3 vary_tangent : TEXCOORD5;
};

float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness);

Texture2D sceneMap : register(t2);
SamplerState sceneMapSampler : register(s2);

float3 BlendNormal(float3 bump1, float3 bump2)
{
    return lerp(bump1, bump2, blend_factor);
}

float4 main(PSInput IN) : SV_Target
{
    float3 vN = IN.vary_normal;
    float3 vT = IN.vary_tangent;
    float3 vB = cross(vN, vT);

    // Generate wave normals (same as waterF.hlsl)
    float2 bigwave = float2(IN.refCoord.w, IN.view.w);

    float3 wave1_a = bumpMap.Sample(bumpMapSampler, bigwave).xyz * 2.0 - 1.0;
    float3 wave2_a = bumpMap.Sample(bumpMapSampler, IN.littleWave.xy).xyz * 2.0 - 1.0;
    float3 wave3_a = bumpMap.Sample(bumpMapSampler, IN.littleWave.zw).xyz * 2.0 - 1.0;

    float3 wave1_b = bumpMap2.Sample(bumpMap2Sampler, bigwave).xyz * 2.0 - 1.0;
    float3 wave2_b = bumpMap2.Sample(bumpMap2Sampler, IN.littleWave.xy).xyz * 2.0 - 1.0;
    float3 wave3_b = bumpMap2.Sample(bumpMap2Sampler, IN.littleWave.zw).xyz * 2.0 - 1.0;

    float3 wave1 = BlendNormal(wave1_a, wave1_b);
    float3 wave2 = BlendNormal(wave2_a, wave2_b);
    float3 wave3 = BlendNormal(wave3_a, wave3_b);

    float3 wavef = (wave1 + wave2 * 0.4 + wave3 * 0.6) * 0.5;

    // Same IBL normal computation as waterF.hlsl
    float3 wave_ibl = wavef * normScale;
    wave_ibl.z *= 2.0;
    wave_ibl = normalize(wave_ibl.x * vT + wave_ibl.y * vB + wave_ibl.z * vN);

    // Water glossiness (same as waterF.hlsl)
    float perceptualRoughness = blurMultiplier * blurMultiplier;
    float glossiness = 1.0 - perceptualRoughness;

    // Derive tc from view-space position via projection rather than
    // gl_FragCoord / screen_res.  screen_res is the deferred buffer size,
    // but this shader may render into a reduced-resolution SSR buffer whose
    // viewport (and thus gl_FragCoord) is smaller, causing an off-centre
    // vignette and incorrect screen-edge fade.
    float4 projPos = mul(projection_matrix, float4(IN.vary_position, 1.0));
    float2 tc = (projPos.xy / projPos.w) * 0.5 + 0.5;

    float4 ssrColor = float4(0.0, 0.0, 0.0, 0.0);
    tapScreenSpaceReflection(1, tc, IN.vary_position, wave_ibl, ssrColor, sceneMap, sceneMapSampler, glossiness);
    return ssrColor;
}
