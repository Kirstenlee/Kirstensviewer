/**
 * @file class1/gltf/pbrmetallicroughnessV.hlsl
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
uniform float4x4 projection_matrix;

uniform int gltf_material_id;

cbuffer GLTFMaterials : register(b0)
{
    float4 gltf_material_data[MAX_UBO_VEC4S];
};

// HLSL globals are implicitly const unless declared static - GLSL has no
// such rule, these are written to below in unpackTextureTransforms().
static float4 texture_base_color_transform[2];
static float4 texture_normal_transform[2];
static float4 texture_metallic_roughness_transform[2];
static float4 texture_emissive_transform[2];
static float4 texture_occlusion_transform[2];

void unpackTextureTransforms()
{
    if (gltf_material_id != -1)
    {
        int idx = gltf_material_id * 12;
        texture_base_color_transform[0] = gltf_material_data[idx + 0];
        texture_base_color_transform[1] = gltf_material_data[idx + 1];
        texture_normal_transform[0] = gltf_material_data[idx + 2];
        texture_normal_transform[1] = gltf_material_data[idx + 3];
        texture_metallic_roughness_transform[0] = gltf_material_data[idx + 4];
        texture_metallic_roughness_transform[1] = gltf_material_data[idx + 5];
        texture_emissive_transform[0] = gltf_material_data[idx + 6];
        texture_emissive_transform[1] = gltf_material_data[idx + 7];
        texture_occlusion_transform[0] = gltf_material_data[idx + 8];
        texture_occlusion_transform[1] = gltf_material_data[idx + 9];
    }
}

// gltfscenemanager.cpp binds this cbuffer at b2 and sets gltf_node_id via
// uniform1i(GLTF_NODE_ID, ...) unconditionally for the non-rigged path.
// Skinning (HAS_SKIN/GLTFJoints) is NOT handled here - DXVertexLayout has no
// HAS_SKIN path.
cbuffer GLTFNodes : register(b2)
{
    float4 gltf_nodes[MAX_NODES_PER_GLTF_OBJECT];
};

uniform int gltf_node_id;

float4x4 getGLTFTransform()
{
    int idx = gltf_node_id * 3;

    float4 src0 = gltf_nodes[idx + 0];
    float4 src1 = gltf_nodes[idx + 1];
    float4 src2 = gltf_nodes[idx + 2];

    // GLSL builds ret[0..3] as COLUMNS; HLSL's ret[i] always means ROW i
    // regardless of storage order (same GLSL-column-vs-HLSL-row trap as
    // objectSkinV.hlsl::getObjectSkinnedTransform()). Build the equivalent
    // rows directly so mul(ret, v) reproduces GLSL's ret * v.
    float4x4 ret;
    ret[0] = float4(src0.x, src1.x, src2.x, src0.w);
    ret[1] = float4(src0.y, src1.y, src2.y, src1.w);
    ret[2] = float4(src0.z, src1.z, src2.z, src2.w);
    ret[3] = float4(0.0, 0.0, 0.0, 1.0);

    return ret;
}

float2 khr_texture_transform(float2 texcoord, float2 scale, float rotation, float2 offset);
float2 texture_transform(float2 vertex_texcoord, float4 khr_gltf_transform[2], float4x4 sl_animation_transform);

// Both vertex_tangent and vertex_normal must already be in the same space
// (eye-space) before this function combines them via cross product - mixing
// object-space and eye-space vectors here is only valid if the transform
// applied afterward is a pure rotation (no scale). GLSL's
// mat2(cos,-sin,sin,cos)*weights rotation is column-major, so it's written
// out as explicit scalar math here rather than HLSL's row-major float2x2.
float3 gltf_tangent_space_transform(float4 vertex_tangent, float3 vertex_normal, float4 khr_gltf_transform[2])
{
    float2 weights = float2(0.0, 1.0);

    // Convert to left-handed coordinate system
    weights.y = -weights.y;

    // Apply KHR_texture_transform (rotation only)
    float khr_rotation = khr_gltf_transform[0].z;
    float cr = cos(khr_rotation);
    float sr = sin(khr_rotation);
    weights = float2(cr * weights.x + sr * weights.y, -sr * weights.x + cr * weights.y);

    // Convert back to right-handed coordinate system
    weights.y = -weights.y;

    // Similar to the MikkTSpace-compatible method of extracting the
    // binormal from the normal and tangent, as seen in the fragment shader
    float3 vertex_binormal = vertex_tangent.w * cross(vertex_normal, vertex_tangent.xyz);

    return (weights.x * vertex_binormal.xyz) + (weights.y * vertex_tangent.xyz);
}

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord0 : TEXCOORD0;
    float4 diffuse_color : COLOR0;
};

#include "varying/pbrMetallicRoughnessVarying.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
    PBRMetallicRoughnessVarying varying;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;
    unpackTextureTransforms();

    // Node transform folded into the modelview multiply, matching GLSL's
    // `mat = modelview_matrix * getGLTFTransform()`.
    float4x4 mat = mul(modelview_matrix, getGLTFTransform());

    float4 pos = mul(mat, float4(IN.position.xyz, 1.0));
    OUT.position = mul(projection_matrix, pos);
    OUT.varying.vary_position = pos.xyz;

    float2 vertex_texcoord = IN.texcoord0;

    float4x4 identity = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    OUT.varying.base_color_uv = texture_transform(vertex_texcoord, texture_base_color_transform, identity);
    OUT.varying.emissive_uv = texture_transform(vertex_texcoord, texture_emissive_transform, identity);
    OUT.varying.vertex_color = IN.diffuse_color;

    // Both n and t transformed to eye-space via the 3x3 upper-left multiply
    // BEFORE calling gltf_tangent_space_transform() - mathematically
    // identical to GLSL's point-offset trick since translation cancels out
    // for any affine mat.
    float3 n = normalize(mul((float3x3)mat, IN.normal));
    float3 t_eye = mul((float3x3)mat, IN.tangent.xyz);
    float3 tan = normalize(gltf_tangent_space_transform(float4(t_eye, IN.tangent.w), n, texture_normal_transform));

    OUT.varying.vary_normal = n;
    OUT.varying.vary_tangent = tan;
    OUT.varying.vary_sign = IN.tangent.w;

    OUT.varying.normal_uv = texture_transform(vertex_texcoord, texture_normal_transform, identity);
    OUT.varying.metallic_roughness_uv = texture_transform(vertex_texcoord, texture_metallic_roughness_transform, identity);
    OUT.varying.occlusion_uv = texture_transform(vertex_texcoord, texture_occlusion_transform, identity);

    // Needed by pbrmetallicroughnessF.hlsl's ALPHA_BLEND branch (shadow-lookup tc).
    OUT.varying.vary_fragcoord = OUT.position.xyz;

    return OUT;
}
