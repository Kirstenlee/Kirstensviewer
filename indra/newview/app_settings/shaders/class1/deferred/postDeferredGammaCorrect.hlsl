/**
 * @file class1/deferred/postDeferredGammaCorrect.hlsl
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

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (attached below, isDeferred=true
// on both gDeferredPostGammaCorrectProgram/gLegacyPostGammaCorrectProgram) -
// moved this file's own texture to t7/s7, matching the established
// free-slot convention used elsewhere in this project.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

uniform float gamma;

// screen_res is also declared (and used) by deferredUtil.hlsl - this copy
// is dead in both the original GLSL and this port (declared, never
// referenced - confirmed via grep of both postDeferredGammaCorrect.glsl
// and this file). Deleted, not guarded.

float3 linear_to_srgb(float3 cl);

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

float3 legacyGamma(float3 color)
{
    float3 c = 1.0 - clamp(color, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    c = 1.0 - pow(c, float3(gamma, gamma, gamma));
    return c;
}

float4 main(PSInput IN) : SV_Target
{
    // S24 (2026-08-06): same GL-vs-D3D11 texture-origin mismatch already
    // fixed elsewhere this session (softenLightF.hlsl's getGBuffer()/
    // getDepth() reads, etc.) - vary_fragcoord itself stays in GL's
    // original convention (postDeferredNoTCV.hlsl), the flip belongs here,
    // at the actual Texture2D .Sample() call site. Never hit until this
    // pass was actually wired into presentDeferredScreen() for the first
    // time (task #121) - confirmed via "upside down" report right after
    // that fix landed.
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);
    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc);
    diff.rgb = linear_to_srgb(diff.rgb);
#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif
    diff.rgb = clamp(diff.rgb, float3(0.0,0.0,0.0), float3(1.0,1.0,1.0));
    return diff;
}
