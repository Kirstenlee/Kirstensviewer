/**
 * @file class1/avatar/eyeballV.hlsl
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

uniform float3x3 normal_matrix;
uniform float4x4 texture_matrix0;
uniform float4x4 modelview_matrix;
uniform float4x4 modelview_projection_matrix;

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float3 normal : NORMAL;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD0;
    float4 position : SV_Position;
};

float4 calcLightingSpecular(float3 pos, float3 norm, float4 color, inout float4 specularColor);
void calcAtmospherics(float3 inPositionEye);

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    float3 pos = mul(modelview_matrix, float4(IN.position.xyz, 1.0)).xyz;
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;

    float3 norm = normalize(mul(normal_matrix, IN.normal));

    calcAtmospherics(pos.xyz);

    float4 specular = float4(1.0, 1.0, 1.0, 1.0);
    float4 color = calcLightingSpecular(pos, norm, IN.diffuse_color, specular);
    OUT.vertex_color = color;

    return OUT;
}
