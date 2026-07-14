/**
 * @file class1/deferred/pbrterrainV.hlsl
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

#define TERRAIN_PBR_DETAIL_EMISSIVE 0
#define TERRAIN_PBR_DETAIL_OCCLUSION -1
#define TERRAIN_PBR_DETAIL_NORMAL -2
#define TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS -3

#define TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE 0
#define TERRAIN_PAINT_TYPE_PBR_PAINTMAP 1

uniform float3x3 normal_matrix;
uniform float4x4 texture_matrix0;
uniform float4x4 modelview_matrix;
uniform float4x4 modelview_projection_matrix;
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
uniform float region_scale;
#endif

// *HACK: Each material uses only one texture transform, but the KHR texture
// transform spec allows handling texture transforms separately for each
// individual texture info.
uniform float4 terrain_texture_transforms[5];

float2 terrain_texture_transform(float2 vertex_texcoord, float4 khr_gltf_transform[2]);
float4 terrain_tangent_space_transform(float4 vertex_tangent, float3 vertex_normal, float4 khr_gltf_transform[2]);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float4 diffuse_color : COLOR0;
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    float2 texcoord1 : TEXCOORD1;
#endif
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_position : TEXCOORD0;
    float3 vary_normal : TEXCOORD1;
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    float3 vary_vertex_normal : TEXCOORD2; // Used by pbrterrainUtilF.hlsl
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    float3 vary_tangents[4] : TEXCOORD3;
    nointerpolation float vary_signs[4] : TEXCOORD7;
#endif

    // vary_texcoord* are used for terrain composition, vary_coords are used for terrain UVs
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    float4 vary_texcoord0 : TEXCOORD11;
    float4 vary_texcoord1 : TEXCOORD12;
#elif TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
    float2 vary_texcoord : TEXCOORD11;
#endif
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    float4 vary_coords[10] : TEXCOORD13;
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
    float4 vary_coords[2] : TEXCOORD13;
#endif
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
    OUT.vary_position = mul(modelview_matrix, float4(IN.position.xyz, 1.0)).xyz;

    float3 n = mul(normal_matrix, IN.normal);
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    OUT.vary_vertex_normal = IN.normal;
#endif
    float3 t = mul(normal_matrix, IN.tangent.xyz);

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    {
        float4 ttt[2];
        float4 transformed_tangent;
        // material 1
        ttt[0].xyz = terrain_texture_transforms[0].xyz;
        ttt[1].x = terrain_texture_transforms[0].w;
        ttt[1].y = terrain_texture_transforms[1].x;
        transformed_tangent = terrain_tangent_space_transform(float4(t, IN.tangent.w), n, ttt);
        OUT.vary_tangents[0] = normalize(transformed_tangent.xyz);
        OUT.vary_signs[0] = transformed_tangent.w;
        // material 2
        ttt[0].xyz = terrain_texture_transforms[1].yzw;
        ttt[1].xy = terrain_texture_transforms[2].xy;
        transformed_tangent = terrain_tangent_space_transform(float4(t, IN.tangent.w), n, ttt);
        OUT.vary_tangents[1] = normalize(transformed_tangent.xyz);
        OUT.vary_signs[1] = transformed_tangent.w;
        // material 3
        ttt[0].xy = terrain_texture_transforms[2].zw;
        ttt[0].z = terrain_texture_transforms[3].x;
        ttt[1].xy = terrain_texture_transforms[3].yz;
        transformed_tangent = terrain_tangent_space_transform(float4(t, IN.tangent.w), n, ttt);
        OUT.vary_tangents[2] = normalize(transformed_tangent.xyz);
        OUT.vary_signs[2] = transformed_tangent.w;
        // material 4
        ttt[0].x = terrain_texture_transforms[3].w;
        ttt[0].yz = terrain_texture_transforms[4].xy;
        ttt[1].xy = terrain_texture_transforms[4].zw;
        transformed_tangent = terrain_tangent_space_transform(float4(t, IN.tangent.w), n, ttt);
        OUT.vary_tangents[3] = normalize(transformed_tangent.xyz);
        OUT.vary_signs[3] = transformed_tangent.w;
    }
#endif
    OUT.vary_normal = normalize(n);

    // Transform and pass tex coords
    {
        float4 ttt[2];
#define transform_xy()             terrain_texture_transform(IN.position.xy,               ttt)
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
// Don't care about upside-down (transform_xy_flipped())
#define transform_yz()             terrain_texture_transform(IN.position.yz,               ttt)
#define transform_negx_z()         terrain_texture_transform(IN.position.xz * float2(-1, 1), ttt)
#define transform_yz_flipped()     terrain_texture_transform(IN.position.yz * float2(-1, 1), ttt)
#define transform_negx_z_flipped() terrain_texture_transform(IN.position.xz,               ttt)
        // material 1
        ttt[0].xyz = terrain_texture_transforms[0].xyz;
        ttt[1].x = terrain_texture_transforms[0].w;
        ttt[1].y = terrain_texture_transforms[1].x;
        OUT.vary_coords[0].xy = transform_xy();
        OUT.vary_coords[0].zw = transform_yz();
        OUT.vary_coords[1].xy = transform_negx_z();
        OUT.vary_coords[1].zw = transform_yz_flipped();
        OUT.vary_coords[2].xy = transform_negx_z_flipped();
        // material 2
        ttt[0].xyz = terrain_texture_transforms[1].yzw;
        ttt[1].xy = terrain_texture_transforms[2].xy;
        OUT.vary_coords[2].zw = transform_xy();
        OUT.vary_coords[3].xy = transform_yz();
        OUT.vary_coords[3].zw = transform_negx_z();
        OUT.vary_coords[4].xy = transform_yz_flipped();
        OUT.vary_coords[4].zw = transform_negx_z_flipped();
        // material 3
        ttt[0].xy = terrain_texture_transforms[2].zw;
        ttt[0].z = terrain_texture_transforms[3].x;
        ttt[1].xy = terrain_texture_transforms[3].yz;
        OUT.vary_coords[5].xy = transform_xy();
        OUT.vary_coords[5].zw = transform_yz();
        OUT.vary_coords[6].xy = transform_negx_z();
        OUT.vary_coords[6].zw = transform_yz_flipped();
        OUT.vary_coords[7].xy = transform_negx_z_flipped();
        // material 4
        ttt[0].x = terrain_texture_transforms[3].w;
        ttt[0].yz = terrain_texture_transforms[4].xy;
        ttt[1].xy = terrain_texture_transforms[4].zw;
        OUT.vary_coords[7].zw = transform_xy();
        OUT.vary_coords[8].xy = transform_yz();
        OUT.vary_coords[8].zw = transform_negx_z();
        OUT.vary_coords[9].xy = transform_yz_flipped();
        OUT.vary_coords[9].zw = transform_negx_z_flipped();
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        // material 1
        ttt[0].xyz = terrain_texture_transforms[0].xyz;
        ttt[1].x = terrain_texture_transforms[0].w;
        ttt[1].y = terrain_texture_transforms[1].x;
        OUT.vary_coords[0].xy = transform_xy();
        // material 2
        ttt[0].xyz = terrain_texture_transforms[1].yzw;
        ttt[1].xy = terrain_texture_transforms[2].xy;
        OUT.vary_coords[0].zw = transform_xy();
        // material 3
        ttt[0].xy = terrain_texture_transforms[2].zw;
        ttt[0].z = terrain_texture_transforms[3].x;
        ttt[1].xy = terrain_texture_transforms[3].yz;
        OUT.vary_coords[1].xy = transform_xy();
        // material 4
        ttt[0].x = terrain_texture_transforms[3].w;
        ttt[0].yz = terrain_texture_transforms[4].xy;
        ttt[1].xy = terrain_texture_transforms[4].zw;
        OUT.vary_coords[1].zw = transform_xy();
#endif
    }

#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    float2 tc = IN.texcoord1.xy;
    OUT.vary_texcoord0.zw = tc.xy;
    OUT.vary_texcoord1.xy = tc.xy-float2(2.0, 0.0);
    OUT.vary_texcoord1.zw = tc.xy-float2(1.0, 0.0);
#elif TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
    OUT.vary_texcoord = IN.position.xy / region_scale;
#endif

    return OUT;
}
