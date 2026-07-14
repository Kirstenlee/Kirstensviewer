/**
 * @file class3/deferred/screenSpaceReflPostV.hlsl
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

uniform float4x4 projection_matrix;
uniform float4x4 inv_proj;

uniform float2 screen_res;

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 vary_fragcoord : TEXCOORD0;
    float3 camera_ray : TEXCOORD1;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    float4 pos = float4(IN.position.xyz, 1.0);
    OUT.position = pos;

    OUT.vary_fragcoord = pos.xy * 0.5 + 0.5;

    float4 rayOrig = mul(inv_proj, float4(pos.xy, 1, 1));
    OUT.camera_ray = rayOrig.xyz / rayOrig.w;

    return OUT;
}
