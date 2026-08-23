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

// Declaration matches its own only use (the #else of HAS_DIFFUSE_LOOKUP
// below) - every current caller of this file that also attaches
// deferredUtil.hlsl (gDeferredFullbrightAlphaMaskAlphaProgram/
// gHUDFullbrightAlphaMaskAlphaProgram, isDeferred=true) also sets indexed
// texturing, so HAS_DIFFUSE_LOOKUP is always defined for them and this
// declaration was previously dead-but-unconditional - colliding with
// deferredUtil.hlsl's normalMap at the same t0/s0. Gating it here (rather
// than deleting, since a future non-indexed caller could still need it)
// removes the collision with zero behavior change for every existing
// caller.
#ifndef HAS_DIFFUSE_LOOKUP
Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
#endif

float3 srgb_to_linear(float3 cs);
float3 linear_to_srgb(float3 cl);
void mirrorClip(float3 pos);

#include "varying/fullbrightVarying.hlsli"

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input -
// this file already had a custom PSInput wrapper (for vary_texture_index)
// but was still missing the SV_Position field itself.
struct PSInput
{
    float4 position : SV_Position;
    FullbrightVarying varying;
#ifdef HAS_DIFFUSE_LOOKUP
    nointerpolation int vary_texture_index : VARYTEXTUREINDEX;
#endif
};

#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha;
#endif

#ifdef IS_ALPHA
// waterPlane is also declared (and used) by deferredUtil.hlsl/waterFogF.hlsl
// (already guarded there) - this copy is dead in both the original GLSL and
// this port (declared, never referenced) - confirmed via grep of both
// fullbrightF.glsl and fullbrightF.hlsl. Deleted, not guarded.
void waterClip(float3 pos);
void calcAtmosphericVars(float3 inPositionEye, float3 light_dir, float ambFactor, out float3 sunlit, out float3 amblit, out float3 additive, out float3 atten);
float4 applySkyAndWaterFog(float3 pos, float3 additive, float3 atten, float4 color);
#endif

float4 main(PSInput IN) : SV_Target
{
#ifdef HAS_DIFFUSE_LOOKUP
    vary_texture_index = IN.vary_texture_index;
#endif

    mirrorClip(IN.varying.vary_position);
#ifdef IS_ALPHA
    waterClip(IN.varying.vary_position.xyz);
#endif

#ifdef HAS_DIFFUSE_LOOKUP
    float4 color = diffuseLookup(IN.varying.vary_texcoord0.xy);
#else
    float4 color = diffuseMap.Sample(diffuseMapSampler, IN.varying.vary_texcoord0.xy);
#endif

    float final_alpha = color.a * IN.varying.vertex_color.a;
#ifdef HAS_ALPHA_MASK
    if (color.a < minimum_alpha) discard;
#endif

    color.rgb *= IN.varying.vertex_color.rgb;
    color.a = final_alpha;

#ifndef IS_HUD
    color.rgb = srgb_to_linear(color.rgb);
#ifdef IS_ALPHA
    float3 sunlit, amblit, additive, atten;
    calcAtmosphericVars(IN.varying.vary_position.xyz, float3(0,0,0), 1.0, sunlit, amblit, additive, atten);
    color.rgb = applySkyAndWaterFog(IN.varying.vary_position, additive, atten, color).rgb;
#endif
#endif

    return max(color, float4(0,0,0,0));
}
