/**
 * @file class1/lighting/sumLightsV.hlsl
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

uniform float4 light_position[8];
uniform float3 light_diffuse[8];

float calcDirectionalLight(float3 n, float3 l);

float3 atmosAmbient();
float3 atmosAffectDirectionalLight(float lightIntensity);
float3 scaleDownLight(float3 light);

float4 sumLights(float3 pos, float3 norm, float4 color)
{
    float4 col = float4(0, 0, 0, 0);
    col.a = color.a;

    col.rgb = light_diffuse[1].rgb * calcDirectionalLight(norm, light_position[1].xyz);
    col.rgb += light_diffuse[1].rgb * sqrt(calcDirectionalLight(norm, -light_position[1].xyz) * 0.5 + 0.25);

    col.rgb = min(col.rgb * color.rgb, 1.0);
    return col;
}
