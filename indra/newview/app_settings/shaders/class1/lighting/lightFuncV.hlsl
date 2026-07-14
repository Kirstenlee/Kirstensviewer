/**
 * @file class1/lighting/lightFuncV.hlsl
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

float calcDirectionalLight(float3 n, float3 l)
{
    float a = max(dot(n, l), 0.0);
    return a;
}

float calcPointLightOrSpotLight(float3 v, float3 n, float4 lp, float3 ln, float la, float fa, float is_pointlight)
{
    //get light vector
    float3 lv = lp.xyz - v;

    //get distance
    float d = length(lv);

    //normalize light vector
    lv *= 1.0 / d;

    //distance attenuation
    float da = clamp(1.0 / (la * d), 0.0, 1.0);

    // spotlight coefficient.
    float spot = max(dot(-ln, lv), is_pointlight);
    da *= spot * spot; // GL_SPOT_EXPONENT=2

    //angular attenuation
    da *= calcDirectionalLight(n, lv);

    return da;
}
