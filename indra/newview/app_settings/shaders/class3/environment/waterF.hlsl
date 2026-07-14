/**
 * @file class3/environment/waterF.hlsl
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

#define WATER_MINIMAL 1

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen);
#endif

float3 scaleSoftClipFragLinear(float3 l);
void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);
float4 applyWaterFogViewLinear(float3 pos, float4 color);

void mirrorClip(float3 pos);

float2 BRDF(float NoV, float roughness);
void calcDiffuseSpecular(float3 baseColor, float metallic, inout float3 diffuseColor, inout float3 specularColor);

void pbrIbl(float3 diffuseColor, float3 specularColor, float3 radiance, float3 irradiance, float ao, float nv, float perceptualRoughness, out float3 diffuse, out float3 specular);

void pbrPunctual(float3 diffuseColor, float3 specularColor, float perceptualRoughness, float metallic, float3 n, float3 v, float3 l, out float nl, out float3 diff, out float3 spec);

float3 pbrBaseLight(float3 diffuseColor, float3 specularColor, float metallic, float3 pos, float3 norm, float perceptualRoughness, float3 light_dir, float3 sunlit, float scol, float3 radiance, float3 irradiance, float3 colorEmissive, float ao, float3 additive, float3 atten);

uniform Texture2D bumpMap : register(t0);
uniform Texture2D bumpMap2 : register(t1);
uniform SamplerState bumpMapSampler : register(s0);
uniform SamplerState bumpMap2Sampler : register(s1);
uniform float blend_factor;

#ifdef TRANSPARENT_WATER
uniform Texture2D screenTex : register(t2);
uniform Texture2D depthMap : register(t3);
uniform SamplerState screenTexSampler : register(s2);
uniform SamplerState depthMapSampler : register(s3);
#endif

uniform Texture2D exclusionTex : register(t4);
uniform SamplerState exclusionTexSampler : register(s4);

uniform int classic_mode;
uniform float3 lightDir;
uniform float3 specular;
uniform float blurMultiplier;
uniform float refScale;
uniform float kd;
uniform float3 normScale;
uniform float fresnelScale;
uniform float3 lightPosition;

float4 main(PSInput IN) : SV_Target
{
    // Placeholder water shader - full conversion requires reading the complete 348-line source
    return float4(0, 0, 0, 1);
}
