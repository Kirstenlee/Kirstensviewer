/**
 * @file class1/deferred/bumpV.hlsl
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

uniform float4x4 modelview_matrix;
uniform float3x3 normal_matrix;
uniform float4x4 texture_matrix0;
uniform float4x4 modelview_projection_matrix;

#ifdef HAS_SKIN
float4x4 getObjectSkinnedTransform();
uniform float4x4 projection_matrix;
#endif

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float3 normal : NORMAL;
    float2 texcoord0 : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_mat0 : TEXCOORD0;
    float3 vary_mat1 : TEXCOORD1;
    float3 vary_mat2 : TEXCOORD2;
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD3;
    float3 vary_position : TEXCOORD4;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
#ifdef HAS_SKIN
    float4x4 mat = getObjectSkinnedTransform();
    mat = mul(modelview_matrix, mat);
    float3 pos = mul(mat, float4(IN.position.xyz, 1.0)).xyz;
    OUT.vary_position = pos;
    OUT.position = mul(projection_matrix, float4(pos, 1.0));

    float3 n = normalize(mul(mat, float4(IN.normal.xyz + IN.position.xyz, 1.0)).xyz - pos.xyz);
    float3 t = normalize(mul(mat, float4(IN.tangent.xyz + IN.position.xyz, 1.0)).xyz - pos.xyz);
#else
    OUT.vary_position = mul(modelview_matrix, float4(IN.position.xyz, 1.0)).xyz;
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
    float3 n = normalize(mul(normal_matrix, IN.normal));
    float3 t = normalize(mul(normal_matrix, IN.tangent.xyz));
#endif

    float3 b = cross(n, t) * IN.tangent.w;
    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;

    OUT.vary_mat0 = float3(t.x, b.x, n.x);
    OUT.vary_mat1 = float3(t.y, b.y, n.y);
    OUT.vary_mat2 = float3(t.z, b.z, n.z);

    OUT.vertex_color = IN.diffuse_color;

    return OUT;
}
