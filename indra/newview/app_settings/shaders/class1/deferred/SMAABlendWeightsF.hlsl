/**
 * @file class1/deferred/SMAABlendWeightsF.hlsl
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

Texture2D edgesTex : register(t0);
SamplerState edgesTexSampler : register(s0);
Texture2D areaTex : register(t1);
SamplerState areaTexSampler : register(s1);
Texture2D searchTex : register(t2);
SamplerState searchTexSampler : register(s2);

float4 SMAABlendingWeightCalculationPS(float2 texcoord,
                                       float2 pixcoord,
                                       float4 offset[3],
                                       Texture2D edgesTex, SamplerState edgesTexSampler,
                                       Texture2D areaTex, SamplerState areaTexSampler,
                                       Texture2D searchTex, SamplerState searchTexSampler,
                                       float4 subsampleIndices);

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
    float2 vary_pixcoord : TEXCOORD1;
    float4 vary_offset[3] : TEXCOORD2;
};

float4 main(PSInput IN) : SV_Target
{
    return SMAABlendingWeightCalculationPS(IN.vary_texcoord0,
                                                 IN.vary_pixcoord,
                                                 IN.vary_offset,
                                                 edgesTex, edgesTexSampler,
                                                 areaTex, areaTexSampler,
                                                 searchTex, searchTexSampler,
                                                 float4(0.0, 0.0, 0.0, 0.0)
                                                 );
}
