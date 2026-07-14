/**
 * @file class1/deferred/SMAA.hlsl
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

#ifdef VERTEX_SHADER
    #define SMAA_INCLUDE_VS 1
    #define SMAA_INCLUDE_PS 0
#else
    #define SMAA_INCLUDE_VS 0
    #define SMAA_INCLUDE_PS 1
#endif

uniform float4 SMAA_RT_METRICS;

// SMAA (Subpixel Morphological Antialiasing)
// Based on Jorge Jimenez' implementation
// This file contains HLSL-ported utility functions for edge detection,
// blending weight calculation, and neighborhood blending.
// The full SMAA algorithm is split across separate shader files.

float2 SMAABlendingWeightCalculationPS(float2 texcoord, float2 pixcoord, float4 offset[3], Texture2D edgesTex, SamplerState edgesTexSampler, Texture2D areaTex, SamplerState areaTexSampler, Texture2D searchTex, SamplerState searchTexSampler, float4 subsampleIndices);

float4 SMAANeighborhoodBlendingPS(float2 texcoord, float4 offset[2], Texture2D colorTex, SamplerState colorTexSampler, Texture2D blendTex, SamplerState blendTexSampler);

void SMAADecodeDiagBilinearAccess(float2 e, out float2 d1, out float2 d2, out float2 d);
float2 SMAASearchDiag1(Texture2D edgesTex, SamplerState edgesTexSampler, float2 texcoord, float2 dir, out float2 e);
float2 SMAASearchDiag2(Texture2D edgesTex, SamplerState edgesTexSampler, float2 texcoord, float2 dir, out float2 e);
float SMAACalculateDiagWeights(Texture2D edgesTex, SamplerState edgesTexSampler, float2 texcoord, float2 e, int4 subsampleIndices);
