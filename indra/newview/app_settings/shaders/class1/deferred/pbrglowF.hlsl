/**
 * @file class1/deferred/pbrglowF.hlsl
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

// forward fullbright implementation for HUDs

Texture2D diffuseMap : register(t0);  //always in sRGB space
SamplerState diffuseMapSampler : register(s0);

uniform float3 emissiveColor;
Texture2D emissiveMap : register(t1);
SamplerState emissiveMapSampler : register(s1);

uniform float minimum_alpha; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()

float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

struct PSInput
{
    float3 vary_position : TEXCOORD0;
    float4 vertex_emissive : COLOR0;
    float2 base_color_texcoord : TEXCOORD1;
    float2 emissive_texcoord : TEXCOORD2;
};

float4 main(PSInput IN) : SV_Target
{
    float4 basecolor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord.xy).rgba;

    if (basecolor.a < minimum_alpha)
    {
        discard;
    }

    float3 emissive = emissiveColor;
    emissive *= srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_texcoord.xy).rgb);

    float lum = max(max(emissive.r, emissive.g), emissive.b);
    lum *= IN.vertex_emissive.a;

    // HUDs are rendered after gamma correction, output in sRGB space
    float4 frag_color;
    frag_color.rgb = float3(0, 0, 0);
    frag_color.a = lum;
    return frag_color;
}
