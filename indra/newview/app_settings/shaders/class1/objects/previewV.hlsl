/**
 * @file class1/objects/previewV.hlsl
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

uniform float3x3 normal_matrix;
uniform float4x4 texture_matrix0;
uniform float4x4 modelview_projection_matrix;

uniform float4 color;

uniform float4 light_position[8];
uniform float3 light_direction[8];
uniform float3 light_attenuation[8];
uniform float3 light_diffuse[8];

//===================================================================================================
//declare these here explicitly to separate them from atmospheric lighting elsewhere to work around
//drivers that are picky about functions being declared but not defined even if they aren't called
float calcDirectionalLight(float3 n, float3 l)
{
    float a = max(dot(n,l),0.0);
    return a;
}

//====================================================================================================

#ifdef HAS_SKIN
float4x4 getObjectSkinnedTransform();
uniform float4x4 modelview_matrix;
uniform float4x4 projection_matrix;
#endif

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    float3 norm;
#ifdef HAS_SKIN
    float4x4 mat = getObjectSkinnedTransform();
    mat = mul(modelview_matrix, mat);
    float4 pos = mul(mat, float4(IN.position.xyz, 1.0));
    OUT.position = mul(projection_matrix, pos);
    norm = normalize(mul(mat, float4(IN.normal.xyz+IN.position.xyz,1.0)).xyz-pos.xyz);
#else
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
    norm = normalize(mul(normal_matrix, IN.normal));
#endif

    OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;

    float4 col = float4(0, 0, 0, 1);

    // Collect normal lights (need to be divided by two, as we later multiply by 2)
    col.rgb += light_diffuse[1].rgb * calcDirectionalLight(norm, light_position[1].xyz);
    col.rgb += light_diffuse[2].rgb * calcDirectionalLight(norm, light_position[2].xyz);
    col.rgb += light_diffuse[3].rgb * calcDirectionalLight(norm, light_position[3].xyz);

    OUT.vertex_color = col*color;

    return OUT;
}
