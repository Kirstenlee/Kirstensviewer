/**
 * @file class1/deferred/pbrterrainF.hlsl
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

#define TERRAIN_PBR_DETAIL_EMISSIVE 0
#define TERRAIN_PBR_DETAIL_OCCLUSION -1
#define TERRAIN_PBR_DETAIL_NORMAL -2
#define TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS -3

#define TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE 0
#define TERRAIN_PAINT_TYPE_PBR_PAINTMAP 1

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
#define TerrainCoord float4[3]
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
#define TerrainCoord float2
#endif

#define MIX_X    1 << 3
#define MIX_Y    1 << 4
#define MIX_Z    1 << 5
#define MIX_W    1 << 6

struct TerrainMix
{
    float4 weight;
    int type;
};

TerrainMix get_terrain_mix_weights(float alpha1, float alpha2, float alphaFinal);
TerrainMix get_terrain_usage_from_weight3(float3 weight3);

struct PBRMix
{
    float4 col;       // RGB color with alpha, linear space
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    float3 orm;       // Occlusion, roughness, metallic
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    float2 rm;        // Roughness, metallic
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    float3 vNt;       // Unpacked normal texture sample, vector
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    float3 emissive;  // RGB emissive color, linear space
#endif
};

PBRMix init_pbr_mix();
PBRMix mix_pbr(PBRMix mix1, PBRMix mix2, float mix2_weight);

#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
Texture2D alpha_ramp : register(t0);
SamplerState alpha_rampSampler : register(s0);
#elif TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
Texture2D paint_map : register(t0);
SamplerState paint_mapSampler : register(s0);
#endif

// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#additional-textures
Texture2D detail_0_base_color : register(t1);
SamplerState detail_0_base_colorSampler : register(s1);
Texture2D detail_1_base_color : register(t2);
SamplerState detail_1_base_colorSampler : register(s2);
Texture2D detail_2_base_color : register(t3);
SamplerState detail_2_base_colorSampler : register(s3);
Texture2D detail_3_base_color : register(t4);
SamplerState detail_3_base_colorSampler : register(s4);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
Texture2D detail_0_normal : register(t5);
SamplerState detail_0_normalSampler : register(s5);
Texture2D detail_1_normal : register(t6);
SamplerState detail_1_normalSampler : register(s6);
Texture2D detail_2_normal : register(t7);
SamplerState detail_2_normalSampler : register(s7);
Texture2D detail_3_normal : register(t8);
SamplerState detail_3_normalSampler : register(s8);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
Texture2D detail_0_metallic_roughness : register(t9);
SamplerState detail_0_metallic_roughnessSampler : register(s9);
Texture2D detail_1_metallic_roughness : register(t10);
SamplerState detail_1_metallic_roughnessSampler : register(s10);
Texture2D detail_2_metallic_roughness : register(t11);
SamplerState detail_2_metallic_roughnessSampler : register(s11);
Texture2D detail_3_metallic_roughness : register(t12);
SamplerState detail_3_metallic_roughnessSampler : register(s12);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
Texture2D detail_0_emissive : register(t13);
SamplerState detail_0_emissiveSampler : register(s13);
Texture2D detail_1_emissive : register(t14);
SamplerState detail_1_emissiveSampler : register(s14);
Texture2D detail_2_emissive : register(t15);
SamplerState detail_2_emissiveSampler : register(s15);
Texture2D detail_3_emissive : register(t16);
SamplerState detail_3_emissiveSampler : register(s16);
#endif

uniform float4 baseColorFactors[4]; // See also vertex_color in pbropaqueV.hlsl
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
uniform float4 metallicFactors;
uniform float4 roughnessFactors;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
uniform float3 emissiveColors[4];
#endif
uniform float4 minimum_alphas; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()

struct PSInput
{
    float3 vary_position : TEXCOORD0;
    float3 vary_normal : TEXCOORD1;
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    float3 vary_vertex_normal : TEXCOORD2;
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

    bool isFrontFace : SV_IsFrontFace;
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

void mirrorClip(float3 position);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);

float terrain_mix(TerrainMix tm, float4 tms4);

PBRMix terrain_sample_and_multiply_pbr(
    TerrainCoord terrain_coord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    , float3 vary_vertex_normal
#endif
    , Texture2D tex_col, SamplerState tex_colSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , Texture2D tex_orm, SamplerState tex_ormSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , Texture2D tex_vNt, SamplerState tex_vNtSampler
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    , float tangent_sign
#endif
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , Texture2D tex_emissive, SamplerState tex_emissiveSampler
#endif
    , float4 factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    , float3 factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , float2 factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , float3 factor_emissive
#endif
    );

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
// from mikktspace.com
float3 mikktspace(float3 vNt, float3 vT, float sign_, float3 vary_normal, bool isFrontFace)
{
    float3 vN = vary_normal;

    float3 vB = sign_ * cross(vN, vT);
    float3 tnorm = normalize( vNt.x * vT + vNt.y * vB + vNt.z * vN );

    tnorm *= isFrontFace ? 1.0 : -1.0;

    return tnorm;
}
#endif

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // Make sure we clip the terrain if we're in a mirror.
    mirrorClip(IN.vary_position);

    TerrainMix tm;
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    float alpha1 = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord0.zw).a;
    float alpha2 = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord1.xy).a;
    float alphaFinal = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord1.zw).a;

    tm = get_terrain_mix_weights(alpha1, alpha2, alphaFinal);
#elif TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
    tm = get_terrain_usage_from_weight3(paint_map.Sample(paint_mapSampler, IN.vary_texcoord).xyz);
#endif

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    // RGB = Occlusion, Roughness, Metal
    // default values, see LLViewerTexture::sDefaultPBRORMImagep
    //   occlusion 1.0
    //   roughness 0.0
    //   metal     0.0
    float3 orm_factors[4];
    orm_factors[0] = float3(1.0, roughnessFactors.x, metallicFactors.x);
    orm_factors[1] = float3(1.0, roughnessFactors.y, metallicFactors.y);
    orm_factors[2] = float3(1.0, roughnessFactors.z, metallicFactors.z);
    orm_factors[3] = float3(1.0, roughnessFactors.w, metallicFactors.w);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    float2 rm_factors[4];
    rm_factors[0] = float2(roughnessFactors.x, metallicFactors.x);
    rm_factors[1] = float2(roughnessFactors.y, metallicFactors.y);
    rm_factors[2] = float2(roughnessFactors.z, metallicFactors.z);
    rm_factors[3] = float2(roughnessFactors.w, metallicFactors.w);
#endif

    PBRMix pbr_mix = init_pbr_mix();
    PBRMix mix2;
    TerrainCoord terrain_texcoord;
    switch (tm.type & MIX_X)
    {
    case MIX_X:
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        terrain_texcoord[0].xy = IN.vary_coords[0].xy;
        terrain_texcoord[0].zw = IN.vary_coords[0].zw;
        terrain_texcoord[1].xy = IN.vary_coords[1].xy;
        terrain_texcoord[1].zw = IN.vary_coords[1].zw;
        terrain_texcoord[2].xy = IN.vary_coords[2].xy;
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        terrain_texcoord = IN.vary_coords[0].xy;
#endif
        mix2 = terrain_sample_and_multiply_pbr(
            terrain_texcoord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , IN.vary_vertex_normal
#endif
            , detail_0_base_color, detail_0_base_colorSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_0_metallic_roughness, detail_0_metallic_roughnessSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_0_normal, detail_0_normalSampler
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , IN.vary_signs[0]
#endif
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , detail_0_emissive, detail_0_emissiveSampler
#endif
            , baseColorFactors[0]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[0]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[0]
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , emissiveColors[0]
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        mix2.vNt = mikktspace(mix2.vNt, IN.vary_tangents[0], IN.vary_signs[0], IN.vary_normal, IN.isFrontFace);
#endif
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.x);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_Y)
    {
    case MIX_Y:
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        terrain_texcoord[0].xy = IN.vary_coords[2].zw;
        terrain_texcoord[0].zw = IN.vary_coords[3].xy;
        terrain_texcoord[1].xy = IN.vary_coords[3].zw;
        terrain_texcoord[1].zw = IN.vary_coords[4].xy;
        terrain_texcoord[2].xy = IN.vary_coords[4].zw;
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        terrain_texcoord = IN.vary_coords[0].zw;
#endif
        mix2 = terrain_sample_and_multiply_pbr(
            terrain_texcoord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , IN.vary_vertex_normal
#endif
            , detail_1_base_color, detail_1_base_colorSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_1_metallic_roughness, detail_1_metallic_roughnessSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_1_normal, detail_1_normalSampler
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , IN.vary_signs[1]
#endif
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , detail_1_emissive, detail_1_emissiveSampler
#endif
            , baseColorFactors[1]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[1]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[1]
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , emissiveColors[1]
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        mix2.vNt = mikktspace(mix2.vNt, IN.vary_tangents[1], IN.vary_signs[1], IN.vary_normal, IN.isFrontFace);
#endif
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.y);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_Z)
    {
    case MIX_Z:
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        terrain_texcoord[0].xy = IN.vary_coords[5].xy;
        terrain_texcoord[0].zw = IN.vary_coords[5].zw;
        terrain_texcoord[1].xy = IN.vary_coords[6].xy;
        terrain_texcoord[1].zw = IN.vary_coords[6].zw;
        terrain_texcoord[2].xy = IN.vary_coords[7].xy;
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        terrain_texcoord = IN.vary_coords[1].xy;
#endif
        mix2 = terrain_sample_and_multiply_pbr(
            terrain_texcoord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , IN.vary_vertex_normal
#endif
            , detail_2_base_color, detail_2_base_colorSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_2_metallic_roughness, detail_2_metallic_roughnessSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_2_normal, detail_2_normalSampler
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , IN.vary_signs[2]
#endif
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , detail_2_emissive, detail_2_emissiveSampler
#endif
            , baseColorFactors[2]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[2]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[2]
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , emissiveColors[2]
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        mix2.vNt = mikktspace(mix2.vNt, IN.vary_tangents[2], IN.vary_signs[2], IN.vary_normal, IN.isFrontFace);
#endif
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.z);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_W)
    {
    case MIX_W:
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        terrain_texcoord[0].xy = IN.vary_coords[7].zw;
        terrain_texcoord[0].zw = IN.vary_coords[8].xy;
        terrain_texcoord[1].xy = IN.vary_coords[8].zw;
        terrain_texcoord[1].zw = IN.vary_coords[9].xy;
        terrain_texcoord[2].xy = IN.vary_coords[9].zw;
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        terrain_texcoord = IN.vary_coords[1].zw;
#endif
        mix2 = terrain_sample_and_multiply_pbr(
            terrain_texcoord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , IN.vary_vertex_normal
#endif
            , detail_3_base_color, detail_3_base_colorSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_3_metallic_roughness, detail_3_metallic_roughnessSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_3_normal, detail_3_normalSampler
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , IN.vary_signs[3]
#endif
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , detail_3_emissive, detail_3_emissiveSampler
#endif
            , baseColorFactors[3]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[3]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[3]
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , emissiveColors[3]
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        mix2.vNt = mikktspace(mix2.vNt, IN.vary_tangents[3], IN.vary_signs[3], IN.vary_normal, IN.isFrontFace);
#endif
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.w);
        break;
    default:
        break;
    }

    float minimum_alpha = terrain_mix(tm, minimum_alphas);
    if (pbr_mix.col.a < minimum_alpha)
    {
        discard;
    }
    float base_color_factor_alpha = terrain_mix(tm, float4(baseColorFactors[0].z, baseColorFactors[1].z, baseColorFactors[2].z, baseColorFactors[3].z));

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    float3 tnorm = normalize(pbr_mix.vNt);
#else
    float3 tnorm = IN.vary_normal;
#endif
    tnorm *= IN.isFrontFace ? 1.0 : -1.0;


#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
#define mix_emissive pbr_mix.emissive
#else
#define mix_emissive float3(0, 0, 0)
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
#define mix_orm pbr_mix.orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
#define mix_orm float3(1.0, pbr_mix.rm.x, pbr_mix.rm.y)
#else
// Matte plastic potato terrain
#define mix_orm float3(1.0, 1.0, 0.0)
#endif
    OUT.data0 = max(float4(pbr_mix.col.xyz, 0.0), float4(0, 0, 0, 0));                                                   // Diffuse
    OUT.data1 = max(float4(mix_orm.rgb, base_color_factor_alpha), float4(0, 0, 0, 0));                                    // PBR linear packed Occlusion, Roughness, Metal.
    OUT.data2 = encodeNormal(tnorm, 0, GBUFFER_FLAG_HAS_PBR); // normal, flags

#if defined(HAS_EMISSIVE)
    OUT.data3 = max(float4(mix_emissive, 0), float4(0, 0, 0, 0));                                                // PBR sRGB Emissive
#endif

    return OUT;
}
