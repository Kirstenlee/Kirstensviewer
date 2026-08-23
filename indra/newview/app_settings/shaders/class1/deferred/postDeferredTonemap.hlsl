/**
 * @file class1/deferred/postDeferredTonemap.hlsl
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

// S24 (2026-08-09, task #164): t0-t3/s0-s3 reserved by deferredUtil.hlsl
// (mFeatures.isDeferred=true on every program using this file) - matches
// the same fix and convention already established in
// postDeferredGammaCorrect.hlsl's own diffuseRect (t7/s7). exposureMap
// (tonemapUtilF.hlsl, attached via mFeatures.hasTonemap) uses t8/s8.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

#ifdef GAMMA_CORRECT
uniform float gamma;
#endif

float3 linear_to_srgb(float3 cl);
float3 toneMap(float3 color);
float3 clampHDRRange(float3 color);

#ifdef GAMMA_CORRECT
float3 legacyGamma(float3 color)
{
    float3 c = 1.0 - clamp(color, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    c = 1.0 - pow(c, float3(gamma, gamma, gamma));
    return c;
}
#endif

float4 main(PSInput IN) : SV_Target
{
    // S24 (2026-08-09, task #164): same GL-vs-D3D11 texture-origin flip
    // already established elsewhere in this post-fx chain (see
    // postDeferredGammaCorrect.hlsl's own identical comment) - never hit
    // until this file was actually wired into presentDeferredScreen() for
    // the first time (this same task), confirmed via "world upside down"
    // report right after that fix landed.
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);
    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc);
#ifndef NO_POST
    diff.rgb = toneMap(diff.rgb);
#else
    diff.rgb = clamp(diff.rgb, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
#endif
#ifdef GAMMA_CORRECT
    diff.rgb = linear_to_srgb(diff.rgb);
#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif
#endif
    diff.rgb = clamp(diff.rgb, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    return diff;
}
