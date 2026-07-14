/**
 * @file class1/windlight/atmosphericsF.hlsl
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

float3 getAdditiveColor();
float3 getAtmosAttenuation();
float3 scaleSoftClipFrag(float3 light);

float3 srgb_to_linear(float3 col);
float3 linear_to_srgb(float3 col);

uniform float sky_hdr_scale;

float3 atmosFragLighting(float3 light, float3 additive, float3 atten)
{
    light *= atten.r;
    additive = srgb_to_linear(additive*2.0);
    additive *= sky_hdr_scale;
    light += additive;
    return light;
}

float3 atmosFragLightingLinear(float3 light, float3 additive, float3 atten)
{
    return atmosFragLighting(light, additive, atten);
}

float3 atmosLighting(float3 light)
{
    return atmosFragLighting(light, getAdditiveColor(), getAtmosAttenuation());
}
