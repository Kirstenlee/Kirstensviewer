/**
 * @file class1/deferred/terrainV.hlsl
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

uniform float4x4 modelview_projection_matrix;
uniform float4x4 modelview_matrix;
uniform float4x4 texture_matrix0;
uniform float3x3 normal_matrix;

// S24 (2026-08-06): were missing entirely - the whole point of this port
// bug (see below). Real GL_OBJECT_LINEAR-texgen-equivalent uniforms, set
// by LLDrawPoolTerrain::renderFullShaderTextures() every frame
// (uniform4fv(OBJECT_PLANE_S/T, ...)) - already correctly wired on the
// C++ side, just never consumed here.
uniform float4 object_plane_s;
uniform float4 object_plane_t;

#include "varying/terrainVarying.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    // S24 (2026-08-06): was "float2 texcoord0 : TEXCOORD0" - terrainV.glsl
    // has no texcoord0 input at all, only "in vec2 texcoord1" - wrong
    // semantic entirely, not just a naming mismatch (see below).
    float2 texcoord1 : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    TerrainVarying varying;
};

// S24 (2026-08-06): real port of terrainV.glsl's texgen_object() - GL's
// legacy GL_OBJECT_LINEAR texture-coordinate generation, done by hand
// since D3D11 has no fixed-function equivalent: s/t = dot(object-space
// position, object_plane_s/t), then transformed by texture_matrix0 (the
// same shape as the GLSL original, not simplified/changed).
float2 texgen_object(float4 vpos, float4x4 mat, float4 tp0, float4 tp1)
{
    float4 tcoord;
    tcoord.x = dot(vpos, tp0);
    tcoord.y = dot(vpos, tp1);
    tcoord.z = 0;
    tcoord.w = 1;
    tcoord = mul(mat, tcoord);
    return tcoord.xy;
}

VSOutput main(VSInput IN)
{
    VSOutput OUT;
    OUT.position = mul(modelview_projection_matrix, float4(IN.position.xyz, 1.0));
    OUT.varying.pos = mul(modelview_matrix, float4(IN.position.xyz, 1.0)).xyz;
    OUT.varying.vary_normal = normalize(mul(normal_matrix, IN.normal));

    // S24 (2026-08-06): this whole block used to collapse all four detail/
    // alpha-ramp UV channels into one repeated, wrongly-sourced value (see
    // git history/session notes) - restored to match terrainV.glsl exactly.
    // vary_texcoord0.xy: detail-texture UV, real object-space texgen (used
    // identically by all 4 detail textures in terrainF.hlsl). vary_texcoord0.zw/
    // vary_texcoord1.xy/.zw: three DIFFERENT alpha-ramp sample coordinates,
    // each a different x-offset slice of the SAME per-vertex texcoord1
    // attribute (the elevation-based blend-ramp lookup, entirely separate
    // per-vertex data from the detail-texture UV above).
    OUT.varying.vary_texcoord0.xy = texgen_object(float4(IN.position, 1.0), texture_matrix0, object_plane_s, object_plane_t);

    float4 t = float4(IN.texcoord1, 0, 1);
    OUT.varying.vary_texcoord0.zw = t.xy;
    OUT.varying.vary_texcoord1.xy = t.xy - float2(2.0, 0.0);
    OUT.varying.vary_texcoord1.zw = t.xy - float2(1.0, 0.0);

    return OUT;
}
