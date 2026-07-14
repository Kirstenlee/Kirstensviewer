/**
 * @file class1/interface/normaldebugV.hlsl
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

uniform float debug_normal_draw_length;

#ifdef HAS_SKIN
float4x4 getObjectSkinnedTransform();
#else
uniform float3x3 normal_matrix;
#endif
uniform float4x4 projection_matrix;
uniform float4x4 modelview_matrix;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
#ifdef HAS_ATTRIBUTE_TANGENT
    float4 tangent : TANGENT;
#endif
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 normal_g : TEXCOORD0;
#ifdef HAS_ATTRIBUTE_TANGENT
    float4 tangent_g : TEXCOORD1;
#endif
};

// *NOTE: Should use the modelview_projection_matrix here in the non-skinned
// case for efficiency, but opting for the simplier implementation for now as
// this is debug code. Also, the skinned version hasn't beeen tested yet.
// world_pos = mat * float4(position.xyz, 1.0)
float4 get_screen_normal(float3 position, float4 world_pos, float3 normal, float4x4 mat)
{
    float4 world_norm = mul(mat, float4((position + normal), 1.0));
    world_norm.xyz -= world_pos.xyz;
    world_norm.xyz = debug_normal_draw_length * normalize(world_norm.xyz);
    world_norm.xyz += world_pos.xyz;
    return mul(projection_matrix, world_norm);
}

VSOutput main(VSInput IN)
{
    VSOutput OUT;

#ifdef HAS_SKIN
    float4x4 mat = getObjectSkinnedTransform();
    mat = mul(modelview_matrix, mat);
#else
    float4x4 mat = modelview_matrix;
#endif

    float4 world_pos = mul(mat, float4(IN.position.xyz,1.0));

    OUT.position = mul(projection_matrix, world_pos);
    OUT.normal_g = get_screen_normal(IN.position.xyz, world_pos, IN.normal.xyz, mat);
#ifdef HAS_ATTRIBUTE_TANGENT
    OUT.tangent_g = get_screen_normal(IN.position.xyz, world_pos, IN.tangent.xyz, mat);
#endif

    return OUT;
}
