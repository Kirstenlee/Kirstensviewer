/**
 * @file class2/interface/reflectionprobeF.hlsl
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

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float4 getPositionWithDepth(float2 pos_screen, float depth);
float getDepth(float2 pos_screen);

float4 sampleReflectionProbesDebug(float3 pos);

float4 main(PSInput IN) : SV_Target
{
    float2  tc           = IN.vary_fragcoord.xy;
    float depth        = getDepth(tc.xy);
    float4  pos          = getPositionWithDepth(tc, depth);

    return max(sampleReflectionProbesDebug(pos.xyz), float4(0, 0, 0, 0));
}
