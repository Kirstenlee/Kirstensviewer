/**
 * @file class1/interface/pathfindingV.hlsl
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

uniform float tint;
uniform float ambiance;
uniform float alpha_scale;

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vertex_color : COLOR0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));

    float3 l1 = float3(-0.75, 1, 1.0)*0.5;
    float3 l2 = float3(0.5, -0.6, 0.4)*0.25;
    float3 l3 = float3(0.5, -0.8, 0.3)*0.5;

    float lit = max(dot(IN.normal, l1), 0.0);
    lit += max(dot(IN.normal, l2), 0.0);
    lit += max(dot(IN.normal, l3), 0.0);

    lit = clamp(lit, ambiance, 1.0);

    OUT.vertex_color = float4(IN.diffuse_color.rgb * tint * lit, IN.diffuse_color.a*alpha_scale);

    return OUT;
}
