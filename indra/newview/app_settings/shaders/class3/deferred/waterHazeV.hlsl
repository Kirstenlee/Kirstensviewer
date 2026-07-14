/**
 * @file class3/deferred/waterHazeV.hlsl
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

uniform float4 waterPlane;

uniform int above_water;

uniform float4x4 modelview_projection_matrix;

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vary_fragcoord : TEXCOORD0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    float4 pos = float4(IN.position.xyz, 1.0);

    if (above_water > 0)
    {
        pos = mul(modelview_projection_matrix, pos);
    }

    OUT.position = pos;

    // appease OSX GLSL compiler/linker by touching all the varyings we said we would
    setAtmosAttenuation(float3(1, 1, 1));
    setAdditiveColor(float3(0, 0, 0));

    OUT.vary_fragcoord = pos;

    return OUT;
}
