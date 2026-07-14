/**
 * @file class1/deferred/sunDiscV.hlsl
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

uniform float4x4 texture_matrix0;
uniform float4x4 modelview_matrix;
uniform float4x4 modelview_projection_matrix;

void calcAtmospherics(float3 eye_pos);

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 vary_texcoord0 : TEXCOORD0;
    float sun_fade : TEXCOORD1;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    float3 offset = float3(0, 0, 50);
    float4 vert = float4(IN.position.xyz - offset, 1.0);
    float4 pos  = mul(modelview_projection_matrix, vert);

    OUT.sun_fade = smoothstep(0.3, 1.0, (IN.position.z + 50) / 512.0f);

    // smash to *almost* far clip plane -- behind clouds but in front of stars
    pos.z = pos.w*0.999999;
    OUT.position = pos;

    calcAtmospherics(pos.xyz);

    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;

    return OUT;
}
