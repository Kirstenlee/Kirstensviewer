/**
 * @file class1/deferred/bumpF.hlsl
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

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D bumpMap : register(t1);
SamplerState bumpMapSampler : register(s1);

uniform float minimum_alpha;

void mirrorClip(float3 pos);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);

#include "varying/deferredBumpVarying.hlsli"

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input.
struct PSInput
{
    float4 position : SV_Position;
    DeferredBumpVarying varying;
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    mirrorClip(IN.varying.vary_position);

    float4 col = diffuseMap.Sample(diffuseMapSampler, IN.varying.vary_texcoord0.xy);

    if (col.a < minimum_alpha)
    {
        discard;
    }
    col *= IN.varying.vertex_color;

    float3 norm = bumpMap.Sample(bumpMapSampler, IN.varying.vary_texcoord0.xy).rgb * 2.0 - 1.0;

    float3 tnorm = float3(dot(norm, IN.varying.vary_mat0),
            dot(norm, IN.varying.vary_mat1),
            dot(norm, IN.varying.vary_mat2));

    OUT.data0 = float4(col.rgb, 0.0);
    OUT.data1 = IN.varying.vertex_color.aaaa; // spec
    float3 nvn = normalize(tnorm);
    OUT.data2 = encodeNormal(nvn, IN.varying.vertex_color.a, GBUFFER_FLAG_HAS_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data3 = float4(0, 0, 0, 0);
#endif

    return OUT;
}
