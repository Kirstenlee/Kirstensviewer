/**
 * @file class1/deferred/fullbrightF.hlsl
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

float3 srgb_to_linear(float3 cs);
float3 linear_to_srgb(float3 cl);
void mirrorClip(float3 pos);

struct PSInput
{
    float3 vary_position : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD1;
};

#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha;
#endif

#ifdef IS_ALPHA
uniform float4 waterPlane;
void waterClip(float3 pos);
void calcAtmosphericVars(float3 inPositionEye, float3 light_dir, float ambFactor, out float3 sunlit, out float3 amblit, out float3 additive, out float3 atten);
float4 applySkyAndWaterFog(float3 pos, float3 additive, float3 atten, float4 color);
#endif

float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.vary_position);
#ifdef IS_ALPHA
    waterClip(IN.vary_position.xyz);
#endif

#ifdef HAS_DIFFUSE_LOOKUP
    float4 color = diffuseLookup(IN.vary_texcoord0.xy);
#else
    float4 color = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);
#endif

    float final_alpha = color.a * IN.vertex_color.a;
#ifdef HAS_ALPHA_MASK
    if (color.a < minimum_alpha) discard;
#endif

    color.rgb *= IN.vertex_color.rgb;
    color.a = final_alpha;

#ifndef IS_HUD
    color.rgb = srgb_to_linear(color.rgb);
#ifdef IS_ALPHA
    float3 sunlit, amblit, additive, atten;
    calcAtmosphericVars(IN.vary_position.xyz, float3(0,0,0), 1.0, sunlit, amblit, additive, atten);
    color.rgb = applySkyAndWaterFog(IN.vary_position, additive, atten, color).rgb;
#endif
#endif

    return max(color, float4(0,0,0,0));
}
