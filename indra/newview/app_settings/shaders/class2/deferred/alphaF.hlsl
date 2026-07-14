/**
 * @file class2/deferred/alphaF.hlsl
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

//class2/deferred/alphaF.glsl

/*[EXTRA_CODE_HERE]*/

#define INDEXED 1
#define NON_INDEXED 2
#define NON_INDEXED_NO_COLOR 3

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

uniform float3x3 env_mat;
uniform float3 sun_dir;
uniform float3 moon_dir;
uniform int classic_mode;
uniform float minimum_alpha;
uniform float4x4 proj_mat;
uniform float4x4 inv_proj;
uniform float2 screen_res;
uniform int sun_up_factor;
uniform float4 light_position[8];
uniform float3 light_direction[8];
uniform float4 light_attenuation[8];
uniform float3 light_diffuse[8];

void waterClip(float3 pos);
float3 srgb_to_linear(float3 c);
float3 linear_to_srgb(float3 c);
float4 applySkyAndWaterFog(float3 pos, float3 additive, float3 atten, float4 color);
void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen);
#endif

float getAmbientClamp();
void mirrorClip(float3 pos);

void sampleReflectionProbesLegacy(inout float3 ambenv, inout float3 glossenv, inout float3 legacyenv,
    float2 tc, float3 pos, float3 norm, float glossiness, float envIntensity, bool transparent, float3 amblit_linear);

float3 calcPointLightOrSpotLight(float3 light_col, float3 diffuse, float3 v, float3 n, float4 lp, float3 ln, float la, float fa, float is_pointlight, float ambiance)
{
    float falloff_factor = (12.0 * fa) - 9.0;
    float inverted_la = falloff_factor / la;

    float3 col = float3(0, 0, 0);
    float3 lv = lp.xyz - v;
    float dist = length(lv);
    lv /= dist;

    float da = clamp(inverted_la / dist, 0.0, 1.0);
    float spot = max(dot(-ln, lv), is_pointlight);
    da *= spot * spot;
    da *= max(dot(n, lv), 0.0);
    return da * light_col;
}

struct PSInput
{
    float3 vary_fragcoord : TEXCOORD0;
    float3 vary_position : TEXCOORD1;
    float2 vary_texcoord0 : TEXCOORD2;
    float3 vary_norm : TEXCOORD3;
#ifdef USE_VERTEX_COLOR
    float4 vertex_color : COLOR0;
#endif
};

float4 main(PSInput IN) : SV_Target
{
    float4 frag_color;
    float3 pos = IN.vary_position;

    float3 norm = normalize(IN.vary_norm);

    float3 light_dir = (sun_up_factor == 1) ? sun_dir : moon_dir;

    float3 sunlit, amblit, atten, additive;
    calcAtmosphericVarsLinear(pos.xyz, norm, light_dir, sunlit, amblit, atten, additive);

    // Alpha testing
    float2 tc = IN.vary_texcoord0.xy;
    // diff_contrib from texture
    float3 diff_contrib = float3(1, 1, 1);

    frag_color = float4(diff_contrib, 1.0);
    return max(frag_color, float4(0, 0, 0, 0));
}
