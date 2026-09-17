/**
 * @file class1/deferred/galacticBandV.hlsl
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

#include "varying/galacticBandVarying.hlsli"

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    GalacticBandVarying varying;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    // LLVOWLSky::mFsSkyVerts is a plain quad already in clip space ([-1,1], see
    // LLVOWLSky::updateGeometry()) - passed straight through, no MVP transform, no dome mesh.
    // z=0.0 pins every pixel to the reversed-Z far plane (near=1.0/far=0.0 - the same trick
    // starsV.hlsl uses), so the depth test only lets this survive where nothing nearer (terrain,
    // water, objects) was already drawn - a screen-space pass needs no dome geometry (and none of
    // its below-horizon "skirt" - see git history) to stay confined to real sky.
    OUT.position = float4(IN.position.xy, 0.0, 1.0);
    OUT.varying.ndc_xy = IN.position.xy;

    return OUT;
}
