/**
 * @file class1/environment/waterV.hlsl
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

// S24 (2026-08-09, task #143): real port of class1/environment/waterV.glsl -
// the previous HLSL body here was a leftover from the initial mechanical
// port, output a completely different (and incomplete) varying set
// (WaterVarying: vary_position/vary_texcoord0-2/vary_eyeVec) than either
// real fragment shader consuming this VS actually needs. This is the ONLY
// waterV.hlsl in the tree (no class2/class3 override), shared by both
// gWaterProgram (waterF.hlsl, above-water) and gUnderWaterProgram
// (underWaterF.hlsl) - underWaterF.hlsl's own PSInput (refCoord/littleWave/
// view/vary_position, TEXCOORD0-3) already expected exactly this output
// shape and was silently mismatched against the old VS the whole time.
//
// VSOutput below is a superset: TEXCOORD0-3 exactly match underWaterF's
// PSInput prefix (order/types identical, so that file needs no changes -
// D3D11 VS/PS linkage matches by semantic name+index, not raw struct
// position, so a PS can freely omit interpolants it doesn't need without
// needing to declare a contiguous prefix). TEXCOORD4-6 (vary_light_dir/
// vary_tangent/vary_normal) and TEXCOORD7 (bigWaveX) are appended after,
// consumed only by the new waterF.hlsl/underWaterF.hlsl.
//
// S24 (2026-08-09, task #146): refCoord.w used to be repurposed to carry
// bigWave.x (packing it into an otherwise-unused varying slot, mirroring
// the original GLSL's own space-saving trick). That broke under D3D11: both
// waterF.hlsl and underWaterF.hlsl derive their screen-space reflection/
// refraction UV via "refCoord.xy / refCoord.z" - a GL-only approximation
// that happens to produce a usable pseudo-perspective-divide because of
// how GL's clip-space Z (range -w..w before divide) relates to its own W;
// D3D11's clip-space Z uses a different range (0..w), so the same divide
// produces a warped, mispositioned result (reported in-world as a
// "fisheye" reflection with no visible ripple - the wave perturbation is
// real but tiny next to how wrong the base UV already is). Real fix: use
// the true clip W (already proven correct via getScreenCoord() in every
// other converted shader - pointLightF.hlsl, spotLightF.hlsl, softenLightF.hlsl)
// instead of Z. refCoord.w now carries the genuine, unmodified clip W;
// bigWave.x moved to its own dedicated TEXCOORD7.

uniform float4x4 modelview_matrix;
uniform float3x3 normal_matrix;
uniform float4x4 modelview_projection_matrix;

void calcAtmospherics(float3 inPositionEye);

uniform float2 waveDir1;
uniform float2 waveDir2;
uniform float time;
uniform float3 eyeVec;
uniform float waterHeight;
uniform float3 lightDir;

struct VSInput
{
    // Real GL source only ever reads "position" - no normal/texcoord0
    // attribute is used (waves are procedural, not mesh-supplied; the
    // constant surface normal/tangent come from normal_matrix * (0,0,1)/
    // (1,0,0) below, not per-vertex data).
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 refCoord : TEXCOORD0;
    float4 littleWave : TEXCOORD1;
    float4 view : TEXCOORD2;
    float3 vary_position : TEXCOORD3;
    float3 vary_light_dir : TEXCOORD4;
    float3 vary_tangent : TEXCOORD5;
    float3 vary_normal : TEXCOORD6;
    float bigWaveX : TEXCOORD7;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    float4 pos = float4(IN.position.xyz, 1.0);

    OUT.vary_position = mul(modelview_matrix, pos).xyz;
    OUT.vary_light_dir = mul(normal_matrix, lightDir);
    OUT.vary_normal = mul(normal_matrix, float3(0, 0, 1));
    OUT.vary_tangent = mul(normal_matrix, float3(1, 0, 0));

    // get view vector
    float3 oEyeVec = pos.xyz - eyeVec;

    float d = length(oEyeVec.xy);
    float ld = min(d, 2560.0);

    pos.xy = eyeVec.xy + oEyeVec.xy / d * ld;
    OUT.view.xyz = oEyeVec;

    d = clamp(ld / 1536.0 - 0.5, 0.0, 1.0);
    d *= d;

    float4 oPosition = float4(IN.position, 1.0);
    oPosition = mul(modelview_projection_matrix, oPosition);

    // Real clip W preserved (see this file's own header comment) - only
    // xyz gets the small Z-bias, matching the original GLSL's own
    // "vec3(0,0,0.2)" additive.
    OUT.refCoord = oPosition;
    OUT.refCoord.z += 0.2;

    // get wave position parameter (create sweeping horizontal waves)
    float3 v = pos.xyz;
    v.x += (cos(v.x * 0.08) + sin(v.y * 0.02)) * 6.0;

    // push position for further horizon effect.
    pos.xyz = oEyeVec.xyz * (waterHeight / oEyeVec.z);
    pos.w = 1.0;
    pos = mul(modelview_matrix, pos);

    calcAtmospherics(pos.xyz);

    // pass wave parameters to pixel shader
    float2 bigWave = (v.xy) * float2(0.04, 0.04) + waveDir1 * time * 0.055;
    // get two normal map (detail map) texture coordinates
    OUT.littleWave.xy = (v.xy) * float2(0.45, 0.9) + waveDir2 * time * 0.13;
    OUT.littleWave.zw = (v.xy) * float2(0.1, 0.2) + waveDir1 * time * 0.1;
    OUT.view.w = bigWave.y;
    OUT.bigWaveX = bigWave.x;

    OUT.position = oPosition;

    return OUT;
}
