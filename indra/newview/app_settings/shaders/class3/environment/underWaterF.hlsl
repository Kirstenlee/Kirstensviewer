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
// waterPlane/waterFogColor/waterFogKS are also declared by waterFogF.hlsl
// (both attached here, hasAtmospherics=true) - include-guarded so
// whichever file concatenates first (this one, being the entry file)
// wins and the other's guarded block is skipped.
#ifndef LL_WATERPLANE_DECLARED
#define LL_WATERPLANE_DECLARED
uniform float4 waterPlane;
#endif
uniform float3 eyeVec;
#ifndef LL_WATERFOGCOLOR_DECLARED
#define LL_WATERFOGCOLOR_DECLARED
uniform float4 waterFogColor;
#endif
uniform float3 waterFogColorLinear;
#ifndef LL_WATERFOGKS_DECLARED
#define LL_WATERFOGKS_DECLARED
uniform float waterFogKS;
#endif
uniform float2 screenRes;

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    // refCoord.w is the real, unmodified clip W (see waterV.hlsl's own
    // comment) - used below for a true perspective divide. bigWave is
    // (bigWaveX, view.w).
    float4 refCoord : TEXCOORD0;
    float4 littleWave : TEXCOORD1;
    float4 view : TEXCOORD2;
    float3 vary_position : TEXCOORD3;
    float bigWaveX : TEXCOORD7;
};

float4 applyWaterFogViewLinearNoClip(float3 pos, float4 color);
void mirrorClip(float3 position);

float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.vary_position);
    // S24 (2026-08-09, task #146): was refCoord.xy/refCoord.z - a GL-only
    // approximation (relies on GL's -w..w clip-space Z range) that produces
    // a warped, mispositioned reflection under D3D11's 0..w range. Fixed
    // to a true perspective divide by the real clip W (see waterV.hlsl's
    // own comment on why refCoord.w is now the genuine clip W rather than
    // a repurposed bigWave.x slot). Not routed through the shared
    // getScreenCoord() (deferredUtil.hlsl) since this shader doesn't set
    // hasReflectionProbes/isDeferred, so that file isn't attached here -
    // inlined instead, same formula.
    float2 screen_tc = (IN.refCoord.xy / IN.refCoord.w) * 0.5 + 0.5;
    // S24 (2026-08-09, task #144, origin sweep): screen_tc is a raw clip-
    // derived screen coordinate (GL's own convention, unflipped - matches
    // every other file's "keep the shared coordinate unflipped, flip only
    // at each .Sample() site" pattern). exclusionTex (mWaterExclusionMask)
    // and screenTex (mWaterDis, sampled below via distort) are both real
    // D3D11 render targets with D3D11's native top-left origin - both
    // reads need the flip, screen_tc itself does not (nothing here uses it
    // for position/NDC reconstruction, unlike deferredUtil.hlsl's pattern).
    float water_mask = exclusionTex.Sample(exclusionTexSampler, float2(screen_tc.x, 1.0 - screen_tc.y)).r;

    float4 color;

    //get detail normals
    float3 wave1 = bumpMap.Sample(bumpMapSampler, float2(IN.bigWaveX, IN.view.w)).xyz*2.0-1.0;
    float3 wave2 = bumpMap.Sample(bumpMapSampler, IN.littleWave.xy).xyz*2.0-1.0;
    float3 wave3 = bumpMap.Sample(bumpMapSampler, IN.littleWave.zw).xyz*2.0-1.0;
    float3 wavef = normalize(wave1+wave2+wave3);

    //figure out distortion vector (ripply)
    float2 distort = screen_tc;
    distort = lerp(distort, distort+wavef.xy*refScale, water_mask);

#ifdef TRANSPARENT_WATER
    // S24 (2026-08-09, task #144, origin sweep): distort is derived from
    // the same unflipped screen_tc above - flip here at the actual
    // .Sample() call, matching exclusionTex's fix above.
    float4 fb = screenTex.Sample(screenTexSampler, float2(distort.x, 1.0 - distort.y));
#else
    float4 fb = float4(waterFogColorLinear, 0.0);
#endif

    fb = applyWaterFogViewLinearNoClip(IN.vary_position, fb);

    return max(fb, float4(0, 0, 0, 0));
}
