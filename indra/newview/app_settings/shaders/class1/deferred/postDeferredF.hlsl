/**
 * @file class1/deferred/postDeferredF.hlsl
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

uniform float4x4 inv_proj;
uniform float2 screen_res;
uniform float max_cof;
uniform float res_scale;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

void dofSample(inout float4 diff, inout float w, float min_sc, float2 tc)
{
    float4 s = diffuseRect.Sample(diffuseRectSampler, tc);

    float sc = abs(s.a * 2.0 - 1.0) * max_cof;

    if (sc > min_sc)
    {
        float wg = 0.25;
        wg += s.r + s.g + s.b;
        diff += wg * s;
        w += wg;
    }
}

void dofSampleNear(inout float4 diff, inout float w, float min_sc, float2 tc)
{
    float4 s = diffuseRect.Sample(diffuseRectSampler, tc);

    float wg = 0.25;
    wg += s.r + s.g + s.b;
    diff += wg * s;
    w += wg;
}

float3 clampHDRRange(float3 color);

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord.xy;

    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc);

    {
        float w = 1.0;
        float sc = (diff.a * 2.0 - 1.0) * max_cof;
        static const float PI = 3.14159265358979323846264;

        if (sc > 0.5)
        {
            while (sc > 0.5)
            {
                int its = int(max(1.0, (sc * 3.7)));
                [unroll]
                for (int i = 0; i < its; ++i)
                {
                    float ang = sc + i * 2 * PI / its;
                    float samp_x = sc * sin(ang);
                    float samp_y = sc * cos(ang);
                    dofSampleNear(diff, w, sc, tc + (float2(samp_x, samp_y) / screen_res));
                }
                sc -= 1.0;
            }
        }
        else if (sc < -0.5)
        {
            sc = abs(sc);
            while (sc > 0.5)
            {
                int its = int(max(1.0, (sc * 3.7)));
                [unroll]
                for (int i = 0; i < its; ++i)
                {
                    float ang = sc + i * 2 * PI / its;
                    float samp_x = sc * sin(ang);
                    float samp_y = sc * cos(ang);
                    dofSample(diff, w, sc, tc + (float2(samp_x, samp_y) / screen_res));
                }
                sc -= 1.0;
            }
        }

        diff /= w;
    }

    diff.rgb = clampHDRRange(diff.rgb);
    return diff;
}
