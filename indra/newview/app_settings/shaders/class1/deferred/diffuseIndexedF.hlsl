/**
 * @file class1/deferred/diffuseIndexedF.hlsl
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

struct PSInput
{
    float3 vary_normal : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD1;
    float3 vary_position : TEXCOORD2;
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

void mirrorClip(float3 pos);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);

float3 linear_to_srgb(float3 c);

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    mirrorClip(IN.vary_position);
    float3 col = IN.vertex_color.rgb * diffuseLookup(IN.vary_texcoord0.xy).rgb;

    float3 spec;
    spec.rgb = float3(IN.vertex_color.a, IN.vertex_color.a, IN.vertex_color.a);

    OUT.data0 = float4(col, 0.0);
    OUT.data1 = float4(spec, IN.vertex_color.a); // spec
    float3 nvn = normalize(IN.vary_normal);
    OUT.data2 = encodeNormal(nvn.xyz, IN.vertex_color.a, GBUFFER_FLAG_HAS_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data3 = float4(0, 0, 0, 0);
#endif

    return OUT;
}
