/**
 * @file class2/deferred/sunLightSSAOF.hlsl
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

//class 2 -- shadows and SSAO

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

float4 getPosition(float2 pos_screen);
float4 getNorm(float2 pos_screen);

float sampleDirectionalShadow(float3 shadow_pos, float3 norm, float2 pos_screen);
float sampleSpotShadow(float3 shadow_pos, float3 norm, int index, float2 pos_screen);
float calcAmbientOcclusion(float4 pos, float3 norm, float2 pos_screen);

float4 main(PSInput IN) : SV_Target
{
    float2 pos_screen = IN.vary_fragcoord.xy;
    float4 pos  = getPosition(pos_screen);
    float4 norm = getNorm(pos_screen);

    float4 col;
    col.r = sampleDirectionalShadow(pos.xyz, norm.xyz, pos_screen);
    col.g = calcAmbientOcclusion(pos, norm.xyz, pos_screen);
    col.b = sampleSpotShadow(pos.xyz, norm.xyz, 0, pos_screen);
    col.a = sampleSpotShadow(pos.xyz, norm.xyz, 1, pos_screen);

    return clamp(col, float4(0, 0, 0, 0), float4(1, 1, 1, 1));
}
