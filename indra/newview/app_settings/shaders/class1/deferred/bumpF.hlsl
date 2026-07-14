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

struct PSInput
{
    float3 vary_mat0 : TEXCOORD0;
    float3 vary_mat1 : TEXCOORD1;
    float3 vary_mat2 : TEXCOORD2;
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD3;
    float3 vary_position : TEXCOORD4;
};

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    mirrorClip(IN.vary_position);

    float4 col = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);

    if (col.a < minimum_alpha)
    {
        discard;
    }
    col *= IN.vertex_color;

    float3 norm = bumpMap.Sample(bumpMapSampler, IN.vary_texcoord0.xy).rgb * 2.0 - 1.0;

    float3 tnorm = float3(dot(norm, IN.vary_mat0),
            dot(norm, IN.vary_mat1),
            dot(norm, IN.vary_mat2));

    OUT.data0 = float4(col.rgb, 0.0);
    OUT.data1 = IN.vertex_color.aaaa; // spec
    float3 nvn = normalize(tnorm);
    OUT.data2 = encodeNormal(nvn, IN.vertex_color.a, GBUFFER_FLAG_HAS_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data3 = float4(0, 0, 0, 0);
#endif

    return OUT;
}
