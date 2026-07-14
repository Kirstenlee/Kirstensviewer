/**
 * @file class1/lighting/lightFuncSpecularV.hlsl
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

float calcDirectionalSpecular(float3 view, float3 n, float3 l)
{
    return pow(max(dot(reflect(view, n), l), 0.0), 8.0);
}

float calcDirectionalLightSpecular(inout float4 specular, float3 view, float3 n, float3 l, float3 lightCol, float da)
{
    specular.rgb += calcDirectionalSpecular(view, n, l) * lightCol * da;
    return max(dot(n, l), 0.0);
}

float3 calcPointLightSpecular(inout float4 specular, float3 view, float3 v, float3 n, float3 l, float r, float pw, float3 lightCol)
{
    //get light vector
    float3 lv = l - v;

    //get distance
    float d = length(lv);

    //normalize light vector
    lv *= 1.0 / d;

    //distance attenuation
    float da = clamp(1.0 / (r * d), 0.0, 1.0);

    //angular attenuation
    da *= calcDirectionalLightSpecular(specular, view, n, lv, lightCol, da);

    return da * lightCol;
}
