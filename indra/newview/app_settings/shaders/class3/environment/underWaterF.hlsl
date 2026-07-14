/**
 * @file class3/environment/underWaterF.hlsl
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

Texture2D bumpMap : register(t0);
SamplerState bumpMapSampler : register(s0);
Texture2D exclusionTex : register(t1);
SamplerState exclusionTexSampler : register(s1);

#ifdef TRANSPARENT_WATER
Texture2D screenTex : register(t2);
SamplerState screenTexSampler : register(s2);
#endif

uniform float4 fogCol;
uniform float3 lightDir;
uniform float3 specular;
uniform float lightExp;
uniform float2 fbScale;
uniform float refScale;
uniform float znear;
uniform float zfar;
uniform float kd;
uniform float4 waterPlane;
uniform float3 eyeVec;
uniform float4 waterFogColor;
uniform float3 waterFogColorLinear;
uniform float waterFogKS;
uniform float2 screenRes;

struct PSInput
{
    //bigWave is (refCoord.w, view.w);
    float4 refCoord : TEXCOORD0;
    float4 littleWave : TEXCOORD1;
    float4 view : TEXCOORD2;
    float3 vary_position : TEXCOORD3;
};

float4 applyWaterFogViewLinearNoClip(float3 pos, float4 color);
void mirrorClip(float3 position);

float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.vary_position);
    float2 screen_tc = (IN.refCoord.xy/IN.refCoord.z) * 0.5 + 0.5;
    float water_mask = exclusionTex.Sample(exclusionTexSampler, screen_tc).r;

    float4 color;

    //get detail normals
    float3 wave1 = bumpMap.Sample(bumpMapSampler, float2(IN.refCoord.w, IN.view.w)).xyz*2.0-1.0;
    float3 wave2 = bumpMap.Sample(bumpMapSampler, IN.littleWave.xy).xyz*2.0-1.0;
    float3 wave3 = bumpMap.Sample(bumpMapSampler, IN.littleWave.zw).xyz*2.0-1.0;
    float3 wavef = normalize(wave1+wave2+wave3);

    //figure out distortion vector (ripply)
    float2 distort = screen_tc;
    distort = lerp(distort, distort+wavef.xy*refScale, water_mask);

#ifdef TRANSPARENT_WATER
    float4 fb = screenTex.Sample(screenTexSampler, distort);
#else
    float4 fb = float4(waterFogColorLinear, 0.0);
#endif

    fb = applyWaterFogViewLinearNoClip(IN.vary_position, fb);

    return max(fb, float4(0, 0, 0, 0));
}
