/**
 * @file class2/deferred/softenLightV.hlsl
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

uniform float2 screen_res;

// forwards
void setAtmosAttenuation(float3 c);
void setAdditiveColor(float3 c);

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 vary_fragcoord : TEXCOORD0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    float4 pos = float4(IN.position.xyz, 1.0);
    OUT.position = pos;

    // appease OSX GLSL compiler/linker by touching all the varyings we said we would
    setAtmosAttenuation(float3(1, 1, 1));
    setAdditiveColor(float3(0, 0, 0));

    // S24 (2026-08-04): REVERTED the V-flip that was here - vary_fragcoord
    // is used for TWO different things downstream, and they need opposite
    // conventions: sampling G-buffer Texture2Ds (diffuseRect/depthMap/
    // lightMap/etc, needs a flip - GL's texture origin is bottom-left,
    // D3D11's is top-left) AND reconstructing world/eye-space position
    // from depth via the inverse projection matrix
    // (getPositionWithDepth()/getScreenCoordinate() in deferredUtil.hlsl,
    // which must NOT be flipped - that math has to stay in the camera's
    // own NDC convention, unrelated to texture-origin conventions).
    // Flipping it here fixed G-buffer color sampling (confirmed - real,
    // correctly-oriented terrain color appeared) but broke position
    // reconstruction for every downstream consumer (atmospherics, PBR
    // lighting) - producing a second, different black-output regression.
    // The flip now lives at the actual texture .Sample() call sites
    // instead (getGBuffer()/getDepth()/getNormRaw() in gbufferUtil.hlsl/
    // deferredUtil.hlsl, and lightMap.Sample() in softenLightF.hlsl) -
    // this keeps vary_fragcoord itself in the same convention GL always
    // used, so getPositionWithDepth() needs no changes at all.
    OUT.vary_fragcoord = (pos.xy*0.5+0.5);

    return OUT;
}
