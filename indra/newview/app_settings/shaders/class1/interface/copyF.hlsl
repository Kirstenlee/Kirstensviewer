/**
 * @file class1/interface/copyF.hlsl
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

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 tc : TEXCOORD0;
};

#if defined(COPY_DEPTH)
Texture2D depthMap : register(t1);
SamplerState depthMapSampler : register(s1);
#endif

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

struct PSOutput
{
    float4 frag_color : SV_Target;
#if defined(COPY_DEPTH)
    float depth : SV_Depth;
#endif
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // S24 (2026-08-09, task #146 follow-up): IN.tc is derived in copyV.hlsl
    // from the fullscreen triangle's own clip-space position
    // (position.xy*0.5+0.5) - the same GL-origin-assuming pattern already
    // found and fixed in softenLightV.hlsl/glow/postDeferredGammaCorrect
    // this session, just never caught here because this shader (gCopyProgram/
    // gCopyDepthProgram) had no live caller until DXDrawPoolWater::
    // beginPostDeferredPass() started using it to snapshot mRT->screen/
    // mRT->deferredScreen into mWaterDis for water's refraction. Both
    // source targets are real D3D11 top-left-origin resources, so sampling
    // them with an unflipped UV wrote an upside-down copy into mWaterDis -
    // confirmed by a real in-world report (fishbowl reflection's apparent
    // position/orientation changed after task #123's getDepth() fix, since
    // water's OWN sampling became correct while the thing it was sampling
    // FROM was still inverted). Flip at the sample site, matching the
    // established pattern - IN.tc itself is only ever used for texture
    // reads in this shader (no position-reconstruction use), so there's no
    // second consumer to keep unflipped.
    float2 tc = float2(IN.tc.x, 1.0 - IN.tc.y);

    OUT.frag_color = diffuseMap.Sample(diffuseMapSampler, tc);
#if defined(COPY_DEPTH)
    OUT.depth = depthMap.Sample(depthMapSampler, tc).r;
#endif

    return OUT;
}
