/**
 * @file class1/deferred/materialV.hlsl
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

#define DIFFUSE_ALPHA_MODE_IGNORE 0
#define DIFFUSE_ALPHA_MODE_BLEND 1
#define DIFFUSE_ALPHA_MODE_MASK 2
#define DIFFUSE_ALPHA_MODE_EMISSIVE 3

uniform float4x4 modelview_matrix;
uniform float4x4 projection_matrix;
uniform float4x4 modelview_projection_matrix;

#ifdef HAS_SKIN
float4x4 getObjectSkinnedTransform();
#else
uniform float3x3 normal_matrix;
#endif

uniform float4x4 texture_matrix0;

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float3 normal : NORMAL;
    float2 texcoord0 : TEXCOORD0;
#ifdef HAS_NORMAL_MAP
    float4 tangent : TANGENT;
    float2 texcoord1 : TEXCOORD1;
#endif
#ifdef HAS_SPECULAR_MAP
    float2 texcoord2 : TEXCOORD2;
#endif
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
#ifdef HAS_NORMAL_MAP
    float3 vary_tangent : TEXCOORD1;
    nointerpolation float vary_sign : TEXCOORD2;
    float3 vary_normal : TEXCOORD3;
    float2 vary_texcoord1 : TEXCOORD4;
#else
    float3 vary_normal : TEXCOORD1;
#endif
#ifdef HAS_SPECULAR_MAP
    float2 vary_texcoord2 : TEXCOORD5;
#endif
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD6;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

#ifdef HAS_SKIN
    float4x4 mat = getObjectSkinnedTransform();

    mat = mul(modelview_matrix, mat);

    float3 pos = mul(mat, float4(IN.position.xyz, 1.0)).xyz;

    OUT.vary_position = pos;

    OUT.position = mul(projection_matrix, float4(pos, 1.0));

#else
    //transform vertex
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));

#endif

    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;

#ifdef HAS_NORMAL_MAP
    OUT.vary_texcoord1 = mul(texture_matrix0, float4(IN.texcoord1, 0, 1)).xy;
#endif

#ifdef HAS_SPECULAR_MAP
    OUT.vary_texcoord2 = mul(texture_matrix0, float4(IN.texcoord2, 0, 1)).xy;
#endif

#ifdef HAS_SKIN
    float3 n = normalize(mul(mat, float4(IN.normal.xyz + IN.position.xyz, 1.0)).xyz - pos.xyz);
#ifdef HAS_NORMAL_MAP
    float3 t = normalize(mul(mat, float4(IN.tangent.xyz + IN.position.xyz, 1.0)).xyz - pos.xyz);

    OUT.vary_tangent = t;
    OUT.vary_sign = IN.tangent.w;
    OUT.vary_normal = n;
#else //HAS_NORMAL_MAP
    OUT.vary_normal = n;
#endif //HAS_NORMAL_MAP
#else //HAS_SKIN
    float3 n = normalize(mul(normal_matrix, IN.normal));
#ifdef HAS_NORMAL_MAP
    float3 t = normalize(mul(normal_matrix, IN.tangent.xyz));

    OUT.vary_tangent = t;
    OUT.vary_sign = IN.tangent.w;
    OUT.vary_normal = n;
#else //HAS_NORMAL_MAP
    OUT.vary_normal = n;
#endif //HAS_NORMAL_MAP
#endif //HAS_SKIN

    OUT.vertex_color = IN.diffuse_color;

#if !defined(HAS_SKIN)
    OUT.vary_position = mul(modelview_matrix, float4(IN.position.xyz, 1.0)).xyz;
#endif

    return OUT;
}
