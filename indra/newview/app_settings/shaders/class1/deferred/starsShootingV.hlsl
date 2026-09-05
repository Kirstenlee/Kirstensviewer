/**
 * @file class1/deferred/starsShootingV.hlsl
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

// S24 (task #279 stage 2, "RENDER WOW"): the shooting-star streak quad -
// geometry is CPU-built fresh every active frame by
// LLVOWLSky::updateShootingStarGeometry(), this just transforms/passes it
// through. Mirrors starsV.hlsl's structure deliberately.

uniform float4x4 modelview_projection_matrix;

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float2 texcoord0 : TEXCOORD0;
};

#include "varying/starsShootingVarying.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
    StarsShootingVarying varying;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    float4 pos = mul(modelview_projection_matrix, float4(IN.position, 1.0));

    // S24: same reversed-Z "smash to far plane" trick as starsV.hlsl - see
    // that file's comment. Avoids writing gl_FragDepth (slow) while keeping
    // streaks from rendering on top of the moon/closer geometry.
    pos.z = 0.0;

    OUT.position = pos;
    OUT.varying.vertex_color = IN.diffuse_color;
    OUT.varying.vary_texcoord0 = IN.texcoord0;

    return OUT;
}
