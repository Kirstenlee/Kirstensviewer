/**
 * @file class1/deferred/alphaV.hlsl
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

#define INDEXED 1
#define NON_INDEXED 2
#define NON_INDEXED_NO_COLOR 3

uniform float3x3 normal_matrix;
uniform float4x4 texture_matrix0;
uniform float4x4 projection_matrix;
uniform float4x4 modelview_matrix;
uniform float4x4 modelview_projection_matrix;

#ifdef USE_INDEXED_TEX
void passTextureIndex();
#endif

#ifdef HAS_SKIN
float4x4 getObjectSkinnedTransform();
#else
#ifdef IS_AVATAR_SKIN
float4x4 getSkinnedTransform();
#endif
#endif

uniform float near_clip;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
#ifdef USE_VERTEX_COLOR
    float4 diffuse_color : COLOR0;
#endif
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_fragcoord : TEXCOORD0;
    float3 vary_position : TEXCOORD1;
#ifdef USE_VERTEX_COLOR
    float4 vertex_color : COLOR0;
#endif
    float2 vary_texcoord0 : TEXCOORD2;
    float3 vary_norm : TEXCOORD3;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    float4 pos;
    float3 norm;

    //transform vertex
#ifdef HAS_SKIN
    float4x4 trans = getObjectSkinnedTransform();
    trans = mul(modelview_matrix, trans);

    pos = mul(trans, float4(IN.position.xyz, 1.0));

    norm = IN.position.xyz + IN.normal.xyz;
    norm = normalize(mul(trans, float4(norm, 1.0)).xyz - pos.xyz);
    float4 frag_pos = mul(projection_matrix, pos);
    OUT.position = frag_pos;
#else

#ifdef IS_AVATAR_SKIN
    float4x4 trans = getSkinnedTransform();
    float4 pos_in = float4(IN.position.xyz, 1.0);
    pos.x = dot(trans[0], pos_in);
    pos.y = dot(trans[1], pos_in);
    pos.z = dot(trans[2], pos_in);
    pos.w = 1.0;

    norm.x = dot(trans[0].xyz, IN.normal);
    norm.y = dot(trans[1].xyz, IN.normal);
    norm.z = dot(trans[2].xyz, IN.normal);
    norm = normalize(norm);

    float4 frag_pos = mul(projection_matrix, pos);
    OUT.position = frag_pos;
#else
    norm = normalize(mul(normal_matrix, IN.normal));
    float4 vert = float4(IN.position.xyz, 1.0);
    pos = mul(modelview_matrix, vert);
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
#endif //IS_AVATAR_SKIN

#endif // HAS_SKIN

#ifdef USE_INDEXED_TEX
    passTextureIndex();
#endif

    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;

    OUT.vary_norm = norm;
    OUT.vary_position = pos.xyz;

#ifdef USE_VERTEX_COLOR
    OUT.vertex_color = IN.diffuse_color;
#endif

#ifdef HAS_SKIN
    OUT.vary_fragcoord.xyz = frag_pos.xyz + float3(0, 0, near_clip);
#else

#ifdef IS_AVATAR_SKIN
    OUT.vary_fragcoord.xyz = pos.xyz + float3(0, 0, near_clip);
#else
    pos = mul(modelview_projection_matrix, vert);
    OUT.vary_fragcoord.xyz = pos.xyz + float3(0, 0, near_clip);
#endif

#endif

    return OUT;
}
