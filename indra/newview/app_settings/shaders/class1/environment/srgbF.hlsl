/**
 * @file class1/environment/srgbF.hlsl
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

float3 srgb_to_linear(float3 cs)
{
    float3 low_range = cs / float3(12.92);
    float3 high_range = pow((cs + float3(0.055)) / float3(1.055), float3(2.4));
    bool3 lte = cs <= float3(0.04045);
#ifdef OLD_SELECT
    float3 result;
    result.r = lte.r ? low_range.r : high_range.r;
    result.g = lte.g ? low_range.g : high_range.g;
    result.b = lte.b ? low_range.b : high_range.b;
    return result;
#else
    return lerp(high_range, low_range, lte);
#endif
}

float4 srgb_to_linear4(float4 cs)
{
    float4 low_range = cs / float4(12.92);
    float4 high_range = pow((cs + float4(0.055)) / float4(1.055), float4(2.4));
    bool4 lte = cs <= float4(0.04045);
#ifdef OLD_SELECT
    float4 result;
    result.r = lte.r ? low_range.r : high_range.r;
    result.g = lte.g ? low_range.g : high_range.g;
    result.b = lte.b ? low_range.b : high_range.b;
    result.a = lte.a ? low_range.a : high_range.a;
    return result;
#else
    return lerp(high_range, low_range, lte);
#endif
}

float3 linear_to_srgb(float3 cl)
{
    cl = clamp(cl, float3(0), float3(1));
    float3 low_range = cl * 12.92;
    float3 high_range = 1.055 * pow(cl, float3(0.41666)) - 0.055;
    bool3 lt = cl < float3(0.0031308);
#ifdef OLD_SELECT
    float3 result;
    result.r = lt.r ? low_range.r : high_range.r;
    result.g = lt.g ? low_range.g : high_range.g;
    result.b = lt.b ? low_range.b : high_range.b;
    return result;
#else
    return lerp(high_range, low_range, lt);
#endif
}

float3 rgb2hsv(float3 c)
{
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-3;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv2rgb(float3 c)
{
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

static const float3x3 inv_ACESOutputMat = float3x3(0.643038, 0.0592687, 0.0059619, 0.311187, 0.931436, 0.063929, 0.0457755, 0.00929492, 0.930118);
static const float3x3 inv_ACESInputMat = float3x3(1.76474, -0.147028, -0.0363368, -0.675778, 1.16025, -0.162436, -0.0889633, -0.0132237, 1.19877);

float3 inv_toneMapACES_Hill(float3 color)
{
    color = mul(inv_ACESOutputMat, color);
    color = inv_RRTAndODTFit(color);
    color = mul(inv_ACESInputMat, color);
    return color;
}
