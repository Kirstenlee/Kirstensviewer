/**
 * @file class1/interface/twotexturecompareF.hlsl
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

Texture2D tex0 : register(t0);
SamplerState tex0Sampler : register(s0);
Texture2D tex1 : register(t1);
SamplerState tex1Sampler : register(s1);
Texture2D dither_tex : register(t2);
SamplerState dither_texSampler : register(s2);
uniform float dither_scale;
uniform float dither_scale_s;
uniform float dither_scale_t;

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
    float2 vary_texcoord1 : TEXCOORD1;
};

float4 main(PSInput IN) : SV_Target
{
    float4 frag_color = abs(tex0.Sample(tex0Sampler, IN.vary_texcoord0.xy) - tex1.Sample(tex1Sampler, IN.vary_texcoord0.xy));

    float2 dither_coord;
    dither_coord[0] = IN.vary_texcoord0[0] * dither_scale_s;
    dither_coord[1] = IN.vary_texcoord0[1] * dither_scale_t;
    float4 dither_vec = dither_tex.Sample(dither_texSampler, dither_coord.xy);

    for(int i = 0; i < 3; i++)
    {
        if(frag_color[i] < dither_vec[i] * dither_scale)
        {
            frag_color[i] = 0.f;
        }
    }

    return frag_color;
}
