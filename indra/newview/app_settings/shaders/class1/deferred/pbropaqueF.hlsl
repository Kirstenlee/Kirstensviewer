/**
 * @file class1/deferred/pbropaqueF.hlsl
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

#ifndef IS_HUD

// deferred opaque implementation

uniform Texture2D diffuseMap : register(t0);
uniform Texture2D bumpMap : register(t1);
uniform Texture2D emissiveMap : register(t2);
uniform Texture2D specularMap : register(t3);
uniform SamplerState diffuseMapSampler : register(s0);
uniform SamplerState bumpMapSampler : register(s1);
uniform SamplerState emissiveMapSampler : register(s2);
uniform SamplerState specularMapSampler : register(s3);

uniform float metallicFactor;
uniform float roughnessFactor;
uniform float3 emissiveColor;

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly. pbropaqueV.hlsl's VSOutput declares
// SV_Position first (consuming register 0), shifting everything after it
// by one - this PSInput had no SV_Position field at all, so its own first
// field started fresh at register 0. Same bug as the bare-Varying-struct
// files, just with hand-written explicit semantics instead of a shared
// struct.
struct PSInput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float3 vary_normal : TEXCOORD1;
    float3 vary_tangent : TEXCOORD2;
    nointerpolation float vary_sign : TEXCOORD3;
    float2 base_color_texcoord : TEXCOORD4;
    float2 normal_texcoord : TEXCOORD5;
    float2 metallic_roughness_texcoord : TEXCOORD6;
    float2 emissive_texcoord : TEXCOORD7;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 target3 : SV_Target3;
#endif
};

uniform float minimum_alpha;

float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

// clipPlane/clipSign are declared (and actually used) by globalF.hlsl's
// real mirrorClip() implementation, attached to every fragment shader -
// this file only forward-declares and calls mirrorClip(), never reads
// either uniform directly, so its own copy here was a dead duplicate
// (X3003 redefinition once both concatenate).
void mirrorClip(float3 pos);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);

uniform float3x3 normal_matrix;

PSOutput main(PSInput IN)
{
    PSOutput OUT;
    mirrorClip(IN.vary_position);

    float4 basecolor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord.xy).rgba;
    basecolor.rgb = srgb_to_linear(basecolor.rgb);
    basecolor *= IN.vertex_color;

    if (basecolor.a < minimum_alpha)
        discard;

    float3 col = basecolor.rgb;

    float3 vNt = bumpMap.Sample(bumpMapSampler, IN.normal_texcoord.xy).xyz * 2.0 - 1.0;
    float sign = IN.vary_sign;
    float3 vN = IN.vary_normal;
    float3 vT = IN.vary_tangent.xyz;
    float3 vB = sign * cross(vN, vT);
    float3 tnorm = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);

    float3 spec = specularMap.Sample(specularMapSampler, IN.metallic_roughness_texcoord.xy).rgb;
    spec.g *= roughnessFactor;
    spec.b *= metallicFactor;

    float3 emissive = emissiveColor;
    emissive *= srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_texcoord.xy).rgb);

    // S24 (2026-08-16, task #155/#157): mechanical-port bug found comparing
    // against pbropaqueF.glsl:106 (`tnorm *= gl_FrontFacing ? 1.0 : -1.0;`)
    // - vary_sign (tangent-handedness, unrelated) had been substituted for
    // the rasterizer-generated front/back flag. A first attempt at the real
    // fix (`SV_IsFrontFace`, same idiom already working in pbrterrainF.hlsl)
    // was reverted the same day after a teleport CTD correlated with it -
    // but never proven as root cause (only correlation: this was the one
    // structurally novel change in that session, on the one shader in the
    // crashing call stack).
    //
    // S24 (2026-09-03, task #266/#271 investigation): re-attempted, this
    // time with real corroborating evidence the original fix was correct
    // and safe - user-reported symptoms (a UV-mirrored mesh showing a
    // razor-straight lighting split down its mirror seam, "Odd shadow.PNG";
    // "silvery" mirrored-UV foliage cards) are the exact, textbook signature
    // of flipping on tangent-mirroring instead of facing. Also: pbralphaF.hlsl
    // already carries this identical `bool isFrontFace : SV_IsFrontFace` +
    // `norm *= IN.isFrontFace ? 1.0 : -1.0` fix live today with no reported
    // crashes (task #155/#157 itself, applied there without incident) - since
    // that's the SAME semantic on a SIBLING PBR shader in the SAME GLTF draw
    // family, it's strong evidence SV_IsFrontFace itself isn't the hazard.
    // If a crash recurs specifically on teleport, that now isolates it to
    // something else in this file, not this semantic in general.
    tnorm *= IN.isFrontFace ? 1.0 : -1.0;

    OUT.target0 = max(float4(col, 0.0), float4(0, 0, 0, 0));
    OUT.target1 = max(float4(spec.rgb, 0.0), float4(0, 0, 0, 0));
    OUT.target2 = encodeNormal(tnorm, 0, GBUFFER_FLAG_HAS_PBR);

#if defined(HAS_EMISSIVE)
    OUT.target3 = max(float4(emissive, 0), float4(0, 0, 0, 0));
#endif
    return OUT;
}

#else

// forward fullbright implementation for HUDs

uniform Texture2D diffuseMap : register(t0);
uniform Texture2D emissiveMap : register(t1);
uniform SamplerState diffuseMapSampler : register(s0);
uniform SamplerState emissiveMapSampler : register(s1);

uniform float3 emissiveColor;

// S24 (2026-08-02): same fix as the non-HUD PSInput above - the paired
// VSOutput (pbropaqueV.hlsl's IS_HUD branch) also declares SV_Position
// first.
struct PSInput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float2 base_color_texcoord : TEXCOORD1;
    float2 emissive_texcoord : TEXCOORD2;
};

uniform float minimum_alpha;

float3 linear_to_srgb(float3 c);
float3 srgb_to_linear(float3 c);

float4 main(PSInput IN) : SV_Target
{
    float4 basecolor = diffuseMap.Sample(diffuseMapSampler, IN.base_color_texcoord.xy).rgba;
    basecolor.a *= IN.vertex_color.a;
    if (basecolor.a < minimum_alpha)
        discard;

    float3 col = IN.vertex_color.rgb * srgb_to_linear(basecolor.rgb);
    float3 emissive = emissiveColor;
    emissive *= srgb_to_linear(emissiveMap.Sample(emissiveMapSampler, IN.emissive_texcoord.xy).rgb);
    col += emissive;

    return float4(linear_to_srgb(col), 0.0);
}

#endif
