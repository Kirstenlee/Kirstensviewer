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

// Shared by both gWaterProgram (waterF.hlsl, above-water) and
// gUnderWaterProgram (underWaterF.hlsl); no class2/class3 override.
// VSOutput's TEXCOORD0-3 match underWaterF's PSInput prefix (D3D11 VS/PS
// linkage matches by semantic name+index, not struct position, so a PS can
// omit interpolants it doesn't need). TEXCOORD4-6 (vary_light_dir/
// vary_tangent/vary_normal) and TEXCOORD7 (bigWaveX) are consumed only by
// waterF.hlsl/underWaterF.hlsl.
//
// refCoord.w carries the genuine, unmodified clip W (not Z): both
// waterF.hlsl and underWaterF.hlsl derive their screen-space reflection UV
// via refCoord.xy/refCoord.z, but D3D11's clip-space Z range (0..w) differs
// from GL's (-w..w), so dividing by Z instead of W produces a warped
// result. Use the true clip W instead (same as getScreenCoord() elsewhere -
// pointLightF.hlsl, spotLightF.hlsl, softenLightF.hlsl).

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
    // Two normal-map layers scrolling in the identical direction at
    // different rates produce a periodic beat (constructive/destructive
    // interference) in the combined surface normal, visible as a slow wave
    // "twitch". Standard fix: give each scrolling layer a distinct
    // direction, not just a distinct speed. Rotated 57 degrees off
    // waveDir1 here, keeping it tied to the EEP wind direction while making
    // the phase drift continuous instead of periodic.
    static const float S24_ROT57_COS = 0.544639035;
    static const float S24_ROT57_SIN = 0.838670568;
    float2 waveDir3 = float2(
        waveDir1.x * S24_ROT57_COS - waveDir1.y * S24_ROT57_SIN,
        waveDir1.x * S24_ROT57_SIN + waveDir1.y * S24_ROT57_COS);
    OUT.littleWave.zw = (v.xy) * float2(0.1, 0.2) + waveDir3 * time * 0.1;
    OUT.view.w = bigWave.y;
    OUT.bigWaveX = bigWave.x;

    OUT.position = oPosition;

    return OUT;
}
