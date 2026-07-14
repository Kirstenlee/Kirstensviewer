/**
 * @file class2/deferred/pbralphaF.hlsl
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

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D bumpMap : register(t1);
SamplerState bumpMapSampler : register(s1);
Texture2D emissiveMap : register(t2);
SamplerState emissiveMapSampler : register(s2);
Texture2D specularMap : register(t3);
SamplerState specularMapSampler : register(s3);

#if defined(HAS_SUN_SHADOW) || defined(HAS_SSAO)
Texture2D lightMap : register(t4);
SamplerState lightMapSampler : register(s4);
#endif

uniform float metallicFactor;
uniform float roughnessFactor;
uniform float3 emissiveColor;
uniform int sun_up_factor;
uniform float3 sun_dir;
uniform float3 moon_dir;
uniform int classic_mode;
uniform float2 screen_res;
uniform float minimum_alpha;
uniform float4 light_position[8];
uniform float3 light_direction[8];
uniform float4 light_attenuation[8];
uniform float3 light_diffuse[8];
uniform float2 light_deferred_attenuation[8];

float3 srgb_to_linear(float3 c);
float3 linear_to_srgb(float3 c);
void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);
float4 applySkyAndWaterFog(float3 pos, float3 additive, float3 atten, float4 color);
void mirrorClip(float3 pos);
void waterClip(float3 pos);
void calcDiffuseSpecular(float3 baseColor, float metallic, inout float3 diffuseColor, inout float3 specularColor);

struct PSInput
{
    float3 vary_fragcoord : TEXCOORD0;
    float3 vary_position : TEXCOORD1;
    float2 base_color_texcoord : TEXCOORD2;
    float2 normal_texcoord : TEXCOORD3;
    float2 metallic_roughness_texcoord : TEXCOORD4;
    float2 emissive_texcoord : TEXCOORD5;
    float4 vertex_color : COLOR0;
    float3 vary_normal : TEXCOORD6;
    float3 vary_tangent : TEXCOORD7;
    nointerpolation float vary_sign : TEXCOORD8;
};

float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.vary_position);
    // PBR alpha rendering implementation
    return float4(0, 0, 0, 0);
}
