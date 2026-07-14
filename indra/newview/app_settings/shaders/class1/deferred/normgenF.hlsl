/**
 * @file class1/deferred/normgenF.hlsl
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

/*[EXTRA_CODE_HERE]*/

// generate a normal map using an approximation of the old emboss bump map "brightness/darkness" technique
// srcMap is a source color image, output should be a normal

Texture2D srcMap : register(t0);
SamplerState srcMapSampler : register(s0);

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
};

uniform float stepX;
uniform float stepY;
uniform float norm_scale;
uniform int bump_code;

#define BE_BRIGHTNESS 1
#define BE_DARKNESS 2

// get luminance or inverse luminance depending on bump_code
float getBumpValue(float2 texcoord)
{
    float3 c = srcMap.Sample(srcMapSampler, texcoord).rgb;

    float3 WEIGHT = float3(0.2995, 0.5875, 0.1145);

    float l = dot(c, WEIGHT);

    if (bump_code == BE_DARKNESS)
    {
        l = 1.0 - l;
    }

    return l;
}


float4 main(PSInput IN) : SV_Target
{
    float c = getBumpValue(IN.vary_texcoord0);

    float scaler = 512.0;

    float3 right = float3(norm_scale, 0, (getBumpValue(IN.vary_texcoord0+float2(stepX, 0))-c)*scaler);
    float3 left = float3(-norm_scale, 0, (getBumpValue(IN.vary_texcoord0-float2(stepX, 0))-c)*scaler);
    float3 up = float3(0, -norm_scale, (getBumpValue(IN.vary_texcoord0-float2(0, stepY))-c)*scaler);
    float3 down = float3(0, norm_scale, (getBumpValue(IN.vary_texcoord0+float2(0, stepY))-c)*scaler);

    float3 norm = cross(right, down) + cross(down, left) + cross(left,up) + cross(up, right);

    norm = normalize(norm);
    norm *= 0.5;
    norm += 0.5;

    return float4(norm, c);
}
