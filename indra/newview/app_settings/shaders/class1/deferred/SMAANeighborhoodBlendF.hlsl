/**
 * @file class1/deferred/SMAANeighborhoodBlendF.hlsl
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
Texture2D blendTex : register(t1);
SamplerState blendTexSampler : register(s1);
#if SMAA_REPROJECTION
Texture2D velocityTex : register(t2);
SamplerState velocityTexSampler : register(s2);
#endif

float4 SMAANeighborhoodBlendingPS(float2 texcoord,
                                  float4 offset,
                                  Texture2D colorTex, SamplerState colorTexSampler,
                                  Texture2D blendTex, SamplerState blendTexSampler
                                  #if SMAA_REPROJECTION
                                  , Texture2D velocityTex, SamplerState velocityTexSampler
                                  #endif
                                  );

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
    float4 vary_offset : TEXCOORD1;
};

float4 main(PSInput IN) : SV_Target
{
    return SMAANeighborhoodBlendingPS(IN.vary_texcoord0,
                                            IN.vary_offset,
                                            diffuseRect, diffuseRectSampler,
                                            blendTex, blendTexSampler
                                            #if SMAA_REPROJECTION
                                            , velocityTex, velocityTexSampler
                                            #endif
                                            );
}
