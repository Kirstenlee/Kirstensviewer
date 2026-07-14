/**
 * @file class1/deferred/starsV.hlsl
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
uniform float4x4 modelview_projection_matrix;
uniform float time;

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD0;
    float2 screenpos : TEXCOORD1;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    float4 pos = mul(modelview_projection_matrix, float4(IN.position, 1.0));

    // smash to far clip plane to
    // avoid rendering on top of moon (do NOT write to gl_FragDepth, it's slow)
    pos.z = pos.w;

    OUT.position = pos;

    float t = fmod(time, 1.25f);
    OUT.screenpos = IN.position.xy * float2(t, t);
    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;
    OUT.vertex_color = IN.diffuse_color;

    return OUT;
}
