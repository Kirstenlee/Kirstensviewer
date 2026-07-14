/**
 * @file class1/interface/pbrTerrainBakeV.hlsl
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

uniform float4x4 modelview_projection_matrix;

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord1 : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vary_texcoord0 : TEXCOORD0;
    float4 vary_texcoord1 : TEXCOORD1;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
    float2 tc = IN.texcoord1.xy;
    OUT.vary_texcoord0.zw = tc.xy;
    OUT.vary_texcoord1.xy = tc.xy-float2(2.0, 0.0);
    OUT.vary_texcoord1.zw = tc.xy-float2(1.0, 0.0);

    return OUT;
}
