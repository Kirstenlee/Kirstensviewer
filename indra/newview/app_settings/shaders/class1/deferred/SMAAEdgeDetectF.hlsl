/**
 * @file class1/deferred/SMAAEdgeDetectF.hlsl
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
#if SMAA_PREDICATION
Texture2D predicationTex : register(t1);
SamplerState predicationTexSampler : register(s1);
#endif

float2 SMAAColorEdgeDetectionPS(float2 texcoord,
                                float4 offset[3],
                                Texture2D colorTex, SamplerState colorTexSampler
                                #if SMAA_PREDICATION
                                , Texture2D predicationTex, SamplerState predicationTexSampler
                                #endif
                                );

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
    float4 vary_offset[3] : TEXCOORD1;
};

float4 main(PSInput IN) : SV_Target
{
    float2 val = SMAAColorEdgeDetectionPS(IN.vary_texcoord0,
                                          IN.vary_offset,
                                          diffuseRect, diffuseRectSampler
                                          #if SMAA_PREDICATION
                                          , predicationTex, predicationTexSampler
                                          #endif
                                          );
    return float4(val, 0.0, 0.0);
}
