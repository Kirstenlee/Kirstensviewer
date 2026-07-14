/**
 * @file class1/deferred/pbrterrainUtilF.hlsl
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

/**
 * Triplanar mapping implementation adapted from Inigo Quilez' example shader,
 * MIT license.
 * https://www.shadertoy.com/view/MtsGWH
 * Copyright (c) 2015 Inigo Quilez
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions: The above copyright
 * notice and this permission notice shall be included in all copies or
 * substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define TERRAIN_PBR_DETAIL_EMISSIVE 0
#define TERRAIN_PBR_DETAIL_OCCLUSION -1
#define TERRAIN_PBR_DETAIL_NORMAL -2
#define TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS -3

#define TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE 0
#define TERRAIN_PAINT_TYPE_PBR_PAINTMAP 1

float3 srgb_to_linear(float3 c);

// A relatively agressive threshold for terrain material mixing sampling
// cutoff. This ensures that only one or two materials are used in most places,
// making PBR terrain blending more performant. Should be greater than 0 to work.
#define TERRAIN_RAMP_MIX_THRESHOLD 0.1
// A small threshold for triplanar mapping sampling cutoff. This and
// TERRAIN_TRIPLANAR_BLEND_FACTOR together ensures that only one or two samples
// per texture are used in most places, making triplanar mapping more
// performant. Should be greater than 0 to work.
// There's also an artistic design choice in the use of these factors, and the
// use of triplanar generally. Don't take these triplanar constants for granted.
#define TERRAIN_TRIPLANAR_MIX_THRESHOLD 0.01

#define SAMPLE_X 1 << 0
#define SAMPLE_Y 1 << 1
#define SAMPLE_Z 1 << 2
#define MIX_X    1 << 3
#define MIX_Y    1 << 4
#define MIX_Z    1 << 5
#define MIX_W    1 << 6

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

PBRMix init_pbr_mix()
{
    PBRMix mix;
    mix.col = float4(0, 0, 0, 0);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm = float3(0, 0, 0);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm = float2(0, 0);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt = float3(0, 0, 0);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = float3(0, 0, 0);
#endif
    return mix;
}

// Usage example, for two weights:
// float2 weights = ... // Weights must add up to 1
// PBRMix mix = init_pbr_mix();
// PBRMix mix1 = ...
// mix = mix_pbr(mix, mix1, weights.x);
// PBRMix mix2 = ...
// mix = mix_pbr(mix, mix2, weights.y);
PBRMix mix_pbr(PBRMix mix1, PBRMix mix2, float mix2_weight)
{
    PBRMix mix;
    mix.col      = mix1.col      + (mix2.col      * mix2_weight);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm      = mix1.orm      + (mix2.orm      * mix2_weight);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm       = mix1.rm       + (mix2.rm       * mix2_weight);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt      = mix1.vNt      + (mix2.vNt      * mix2_weight);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = mix1.emissive + (mix2.emissive * mix2_weight);
#endif
    return mix;
}

PBRMix sample_pbr(
    float2 uv
    , Texture2D tex_col, SamplerState tex_colSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , Texture2D tex_orm, SamplerState tex_ormSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , Texture2D tex_vNt, SamplerState tex_vNtSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , Texture2D tex_emissive, SamplerState tex_emissiveSampler
#endif
    )
{
    PBRMix mix;
    mix.col = tex_col.Sample(tex_colSampler, uv);
    mix.col.rgb = srgb_to_linear(mix.col.rgb);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm = tex_orm.Sample(tex_ormSampler, uv).xyz;
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm = tex_orm.Sample(tex_ormSampler, uv).yz;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt = tex_vNt.Sample(tex_vNtSampler, uv).xyz*2.0-1.0;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = srgb_to_linear(tex_emissive.Sample(tex_emissiveSampler, uv).xyz);
#endif
    return mix;
}

struct TerrainTriplanar
{
    float3 weight;
    int type;
};

struct TerrainMix
{
    float4 weight;
    int type;
};

TerrainMix get_terrain_mix_weights(float alpha1, float alpha2, float alphaFinal)
{
    TerrainMix tm;
    float4 sample_x = float4(1, 0, 0, 0);
    float4 sample_y = float4(0, 1, 0, 0);
    float4 sample_z = float4(0, 0, 1, 0);
    float4 sample_w = float4(0, 0, 0, 1);

    tm.weight = lerp( lerp(sample_w, sample_z, alpha2), lerp(sample_y, sample_x, alpha1), alphaFinal );
    tm.weight -= TERRAIN_RAMP_MIX_THRESHOLD;
    int4 usage = max(int4(0, 0, 0, 0), int4(ceil(tm.weight)));
    // Prevent negative weights and keep weights balanced
    tm.weight = tm.weight*float4(usage);
    tm.weight /= (tm.weight.x + tm.weight.y + tm.weight.z + tm.weight.w);

    tm.type = (usage.x * MIX_X) |
              (usage.y * MIX_Y) |
              (usage.z * MIX_Z) |
              (usage.w * MIX_W);
    return tm;
}

// A paintmap weight applier for 4 swatches. The input saves a channel by not
// storing swatch 1, and assuming the weights of the 4 swatches add to 1.
// The components of weight3 should be between 0 and 1
// The sum of the components of weight3 should be between 0 and 1
TerrainMix get_terrain_usage_from_weight3(float3 weight3)
{
    // These steps ensure the output weights add to between 0 and 1
    weight3.xyz = max(float3(0.0, 0.0, 0.0), weight3.xyz);
    weight3.xyz /= max(1.0, weight3.x + weight3.y + weight3.z);

    TerrainMix tm;

    // Extract the first weight from the other weights
    tm.weight.x = 1.0 - (weight3.x + weight3.y + weight3.z);
    tm.weight.yzw = weight3.xyz;
    int4 usage = max(int4(0, 0, 0, 0), int4(ceil(tm.weight)));

    tm.type = (usage.x * MIX_X) |
              (usage.y * MIX_Y) |
              (usage.z * MIX_Z) |
              (usage.w * MIX_W);
    return tm;
}

// Inverse of get_terrain_usage_from_weight3, excluding usage flags
// The components of weight should be between 0 and 1
// The sum of the components of weight should be 1
float3 get_weight3_from_terrain_weight(float4 weight)
{
    // These steps ensure the input weights add to 1
    weight = max(float4(0.0, 0.0, 0.0, 0.0), weight);
    weight.x += 1.0 - sign(weight.x + weight.y + weight.z + weight.w);
    weight /= weight.x + weight.y + weight.z + weight.w;

    // Then return the input weights with the first weight truncated
    return weight.yzw;
}

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
TerrainTriplanar _t_triplanar(float3 vary_vertex_normal)
{
    float sharpness = TERRAIN_TRIPLANAR_BLEND_FACTOR;
    float threshold = TERRAIN_TRIPLANAR_MIX_THRESHOLD;
    float3 weight_signed = pow(abs(vary_vertex_normal), float3(sharpness, sharpness, sharpness));
    weight_signed /= (weight_signed.x + weight_signed.y + weight_signed.z);
    weight_signed -= float3(threshold, threshold, threshold);
    TerrainTriplanar tw;
    // *NOTE: Make sure the threshold doesn't affect the materials
    tw.weight = max(float3(0, 0, 0), weight_signed);
    tw.weight /= (tw.weight.x + tw.weight.y + tw.weight.z);
    int3 usage = int3(round(max(float3(0, 0, 0), sign(weight_signed))));
    tw.type = ((usage.x) * SAMPLE_X) |
              ((usage.y) * SAMPLE_Y) |
              ((usage.z) * SAMPLE_Z);
    return tw;
}
#endif


// Assume weights add to 1
float terrain_mix(TerrainMix tm, float4 tms4)
{
    return (tm.weight.x * tms4[0]) +
           (tm.weight.y * tms4[1]) +
           (tm.weight.z * tms4[2]) +
           (tm.weight.w * tms4[3]);
}

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
// Triplanar mapping

// Pre-transformed texture coordinates for each axial uv slice (Packing: xy, yz, (-x)z, unused)
#define TerrainCoord float4[3]

// If sign_or_zero is positive, use uv_unflippped, otherwise use uv_flipped
float2 _t_uv(float2 uv_unflipped, float2 uv_flipped, float sign_or_zero)
{
    return lerp(uv_flipped, uv_unflipped, max(0.0, sign_or_zero));
}

float3 _t_normal_post_1(float3 vNt0, float sign_or_zero)
{
    // Assume normal is unpacked
    float3 vNt1 = vNt0;
    // Get sign
    float sign_ = sign_or_zero;
    // Handle case where sign is 0
    sign_ = (2.0*sign_) + 1.0;
    sign_ /= abs(sign_);
    // If the sign is negative, rotate normal by 180 degrees
    vNt1.xy = (min(0, sign_) * vNt1.xy) + (min(0, -sign_) * -vNt1.xy);
    return vNt1;
}

// Triplanar-specific normal texture fixes
float3 _t_normal_post_x(float3 vNt0, float tangent_sign, float3 vary_vertex_normal)
{
    float3 vNt_x = _t_normal_post_1(vNt0, sign(vary_vertex_normal.x));
    // *HACK: Transform normals according to orientation of the UVs
    vNt_x.xy = float2(-vNt_x.y, vNt_x.x);
    vNt_x.xy *= tangent_sign;
    return vNt_x;
}
float3 _t_normal_post_y(float3 vNt0, float3 vary_vertex_normal)
{
    float3 vNt_y = _t_normal_post_1(vNt0, sign(vary_vertex_normal.y));
    // *HACK: Transform normals according to orientation of the UVs
    vNt_y.xy = -vNt_y.xy;
    return vNt_y;
}
float3 _t_normal_post_z(float3 vNt0, float3 vary_vertex_normal)
{
    float3 vNt_z = _t_normal_post_1(vNt0, sign(vary_vertex_normal.z));
    return vNt_z;
}

PBRMix terrain_sample_pbr(
    TerrainCoord terrain_coord
    , TerrainTriplanar tw
    , float3 vary_vertex_normal
    , Texture2D tex_col, SamplerState tex_colSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , Texture2D tex_orm, SamplerState tex_ormSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , Texture2D tex_vNt, SamplerState tex_vNtSampler
    , float tangent_sign
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , Texture2D tex_emissive, SamplerState tex_emissiveSampler
#endif
    )
{
    PBRMix mix = init_pbr_mix();

#define get_uv_x() _t_uv(terrain_coord[0].zw, terrain_coord[1].zw, sign(vary_vertex_normal.x))
#define get_uv_y() _t_uv(terrain_coord[1].xy, terrain_coord[2].xy, sign(vary_vertex_normal.y))
#define get_uv_z() _t_uv(terrain_coord[0].xy, float2(0, 0),        sign(vary_vertex_normal.z))
    switch (tw.type & SAMPLE_X)
    {
    case SAMPLE_X:
        PBRMix mix_x = sample_pbr(
            get_uv_x()
            , tex_col, tex_colSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm, tex_ormSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt, tex_vNtSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive, tex_emissiveSampler
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        // Triplanar-specific normal texture fix
        mix_x.vNt = _t_normal_post_x(mix_x.vNt, tangent_sign, vary_vertex_normal);
#endif
        mix = mix_pbr(mix, mix_x, tw.weight.x);
        break;
    default:
        break;
    }

    switch (tw.type & SAMPLE_Y)
    {
    case SAMPLE_Y:
        PBRMix mix_y = sample_pbr(
            get_uv_y()
            , tex_col, tex_colSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm, tex_ormSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt, tex_vNtSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive, tex_emissiveSampler
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        // Triplanar-specific normal texture fix
        mix_y.vNt = _t_normal_post_y(mix_y.vNt, vary_vertex_normal);
#endif
        mix = mix_pbr(mix, mix_y, tw.weight.y);
        break;
    default:
        break;
    }

    switch (tw.type & SAMPLE_Z)
    {
    case SAMPLE_Z:
        PBRMix mix_z = sample_pbr(
            get_uv_z()
            , tex_col, tex_colSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm, tex_ormSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt, tex_vNtSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive, tex_emissiveSampler
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        // Triplanar-specific normal texture fix
        // *NOTE: Bottom face has not been tested
        mix_z.vNt = _t_normal_post_z(mix_z.vNt, vary_vertex_normal);
#endif
        mix = mix_pbr(mix, mix_z, tw.weight.z);
        break;
    default:
        break;
    }

    return mix;
}

#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1

#define TerrainCoord float2

#endif

PBRMix multiply_factors_pbr(
    PBRMix mix_in
    , float4 factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    , float3 factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , float2 factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , float3 factor_emissive
#endif
    )
{
    PBRMix mix = mix_in;
    mix.col *= factor_col;
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm *= factor_orm;
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm *= factor_rm;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive *= factor_emissive;
#endif
    return mix;
}

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
    )
{
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    PBRMix mix = terrain_sample_pbr(
        terrain_coord
        , _t_triplanar(vary_vertex_normal)
        , vary_vertex_normal
        , tex_col, tex_colSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        , tex_orm, tex_ormSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        , tex_vNt, tex_vNtSampler
        , tangent_sign
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
        , tex_emissive, tex_emissiveSampler
#endif
        );
#else
    PBRMix mix = sample_pbr(
        terrain_coord
        , tex_col, tex_colSampler
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        , tex_orm, tex_ormSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        , tex_vNt, tex_vNtSampler
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
        , tex_emissive, tex_emissiveSampler
#endif
        );
#endif

    mix = multiply_factors_pbr(mix
        , factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
        , factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        , factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
        , factor_emissive
#endif
    );

    return mix;
}
