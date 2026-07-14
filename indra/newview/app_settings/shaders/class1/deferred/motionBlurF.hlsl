/**
 * @file class1/deferred/motionBlurF.hlsl
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

Texture2D diffuseRect : register(t0);
SamplerState diffuseRectSampler : register(s0);
Texture2D velocityMap : register(t1);
SamplerState velocityMapSampler : register(s1);

uniform float2 screen_res;
uniform int motion_blur_strength;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    float2 uv = IN.vary_fragcoord;
    float2 vel = velocityMap.Sample(velocityMapSampler, uv).rg;

    // NDC velocity to pixel velocity
    float2 pixel_vel = vel * screen_res * 0.5;
    float speed = length(pixel_vel);

    // Early out for negligible motion
    if (speed < 0.5)
    {
        return diffuseRect.Sample(diffuseRectSampler, uv);
    }

    // Clamp to max blur length
    float max_blur = float(motion_blur_strength);
    if (speed > max_blur)
    {
        pixel_vel *= max_blur / speed;
    }

    // Step size in UV space per iteration
    float2 step_uv = (pixel_vel / screen_res) * (2.0 / 32.0);

    // Start sampling ahead of center
    float2 sample_uv = uv + step_uv * 16.0;

    // 32-sample triangle-weighted blur
    float3 color = float3(0.0, 0.0, 0.0);
    float total = 0.0;

    for (int i = 0; i < 32; ++i)
    {
        float w = 32.0 - abs(float(i) - 16.0);
        total += w;
        color += diffuseRect.Sample(diffuseRectSampler, sample_uv).rgb * w;
        sample_uv -= step_uv;
    }

    return float4(color / total, 1.0);
}
