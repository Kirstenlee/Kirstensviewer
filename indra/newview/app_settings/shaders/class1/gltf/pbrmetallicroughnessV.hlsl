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

// S24 (task #154): was never ported from pbrmetallicroughnessV.glsl - every
// GLTF object rendered using only its root transform, ignoring per-node
// offsets in the asset's scene graph. gltfscenemanager.cpp already binds
// this cbuffer at b2 (see its own comment, task #79) and sets gltf_node_id
// via uniform1i(GLTF_NODE_ID, ...) unconditionally for the non-rigged path,
// exactly like gltf_material_id above - both just needed the HLSL side.
// Skinning (HAS_SKIN/GLTFJoints) is NOT ported here - a separate, larger,
// already-tracked gap (DXVertexLayout has no HAS_SKIN path).
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

    // GLSL builds ret[0..3] as COLUMNS (mat4[i] indexes columns in GLSL:
    // ret[0]=vec4(src0.xyz,0), ret[1]=vec4(src1.xyz,0), ret[2]=vec4(src2.xyz,0),
    // ret[3]=vec4(src0.w,src1.w,src2.w,1)). HLSL's ret[i] always means ROW i
    // regardless of storage order - the same GLSL-column-vs-HLSL-row trap
    // already fixed in objectSkinV.hlsl::getObjectSkinnedTransform() (see
    // that file's comment for the general pattern/lesson). Build the
    // equivalent rows directly so mul(ret, v) reproduces the exact same
    // numeric matrix as GLSL's ret * v, instead of transliterating the
    // column assignments verbatim (which would silently corrupt every
    // GLTF node transform under DX_RENDER).
    float4x4 ret;
    ret[0] = float4(src0.x, src1.x, src2.x, src0.w);
    ret[1] = float4(src0.y, src1.y, src2.y, src1.w);
    ret[2] = float4(src0.z, src1.z, src2.z, src2.w);
    ret[3] = float4(0.0, 0.0, 0.0, 1.0);

    return ret;
}

float2 khr_texture_transform(float2 texcoord, float2 scale, float rotation, float2 offset);
float2 texture_transform(float2 vertex_texcoord, float4 khr_gltf_transform[2], float4x4 sl_animation_transform);

// S24 (2026-08-19, task #239, task #227 audit finding): this file was
// calling the WRONG function - the shared tangent_space_transform() from
// textureUtilV.hlsl (a different function, with SL-animation/scale-sign
// terms that don't exist in the GLTF-local original) - instead of
// pbrmetallicroughnessV.glsl's own LOCAL gltf_tangent_space_transform().
// Worse, it passed that shared function the raw OBJECT-SPACE IN.tangent
// together with the already EYE-SPACE n, then applied `mat` to the
// RESULT afterward - mixing object-space and eye-space vectors in a
// cross-product is only valid if `mat` is a pure rotation (no scale),
// silently breaking normal-map lighting direction on any scaled glTF
// mesh. Real gltf_tangent_space_transform() ported below, faithfully -
// GLSL's mat2(cos,-sin,sin,cos)*weights rotation is column-major, so it's
// written out as explicit scalar math here rather than reproduced with
// HLSL's row-major float2x2 (same GLSL-column-vs-HLSL-row lesson already
// applied in this file's own getGLTFTransform(), see its comment).
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

    // S24 (task #154): node transform folded into the modelview multiply,
    // matching GLSL's `mat = modelview_matrix * getGLTFTransform()` - was
    // missing entirely before (position/normal/tangent all used
    // modelview_matrix directly, ignoring per-node offsets).
    float4x4 mat = mul(modelview_matrix, getGLTFTransform());

    float4 pos = mul(mat, float4(IN.position.xyz, 1.0));
    OUT.position = mul(projection_matrix, pos);
    OUT.varying.vary_position = pos.xyz;

    float2 vertex_texcoord = IN.texcoord0;

    float4x4 identity = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    OUT.varying.base_color_uv = texture_transform(vertex_texcoord, texture_base_color_transform, identity);
    OUT.varying.emissive_uv = texture_transform(vertex_texcoord, texture_emissive_transform, identity);
    OUT.varying.vertex_color = IN.diffuse_color;

    // S24 (task #239): both n and t transformed to eye-space via the plain
    // 3x3 upper-left multiply BEFORE calling gltf_tangent_space_transform()
    // - mathematically identical to GLSL's point-offset trick
    // ((mat*vec4(v+p,1)).xyz-pos.xyz reduces algebraically to mat3x3*v for
    // any affine mat, translation cancels out), just reusing the form this
    // file already established for n. See the function's own comment above
    // for what was wrong before.
    float3 n = normalize(mul((float3x3)mat, IN.normal));
    float3 t_eye = mul((float3x3)mat, IN.tangent.xyz);
    float3 tan = normalize(gltf_tangent_space_transform(float4(t_eye, IN.tangent.w), n, texture_normal_transform));

    OUT.varying.vary_normal = n;
    OUT.varying.vary_tangent = tan;
    OUT.varying.vary_sign = IN.tangent.w;

    OUT.varying.normal_uv = texture_transform(vertex_texcoord, texture_normal_transform, identity);
    OUT.varying.metallic_roughness_uv = texture_transform(vertex_texcoord, texture_metallic_roughness_transform, identity);
    OUT.varying.occlusion_uv = texture_transform(vertex_texcoord, texture_occlusion_transform, identity);

    // S24 (task #238): needed by pbrmetallicroughnessF.hlsl's newly-ported
    // ALPHA_BLEND lit branch (shadow-lookup tc) - matches
    // pbrmetallicroughnessV.glsl's `vary_fragcoord = vert.xyz;` where
    // vert == the clip-space position, same value as OUT.position here.
    OUT.varying.vary_fragcoord = OUT.position.xyz;

    return OUT;
}
