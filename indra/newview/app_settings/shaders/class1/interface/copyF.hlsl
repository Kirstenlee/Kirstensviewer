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
    // SV_Position required here - its absence shifts every interpolant register (see uiF.hlsl).
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

    // GL-vs-D3D11 texture-origin flip, same pattern as softenLightV.hlsl/
    // glow/postDeferredGammaCorrect. Used by
    // DXDrawPoolWater::beginPostDeferredPass() to snapshot mRT->screen/
    // mRT->deferredScreen into mWaterDis for water's refraction - both
    // source targets are D3D11 top-left-origin resources, so an unflipped
    // UV here would write an upside-down copy into mWaterDis. IN.tc is only
    // ever used for texture reads in this shader, so there's no second
    // consumer to keep unflipped.
    float2 tc = float2(IN.tc.x, 1.0 - IN.tc.y);

    OUT.frag_color = diffuseMap.Sample(diffuseMapSampler, tc);
#if defined(COPY_DEPTH)
    OUT.depth = depthMap.Sample(depthMapSampler, tc).r;
#endif

    return OUT;
}
