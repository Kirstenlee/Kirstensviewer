/**
 * @file class2/deferred/alphaF.hlsl
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

//class2/deferred/alphaF.glsl

/*[EXTRA_CODE_HERE]*/

#define INDEXED 1
#define NON_INDEXED 2
#define NON_INDEXED_NO_COLOR 3

// USE_DIFFUSE_TEX is the alternate to USE_INDEXED_TEX - used by
// gDeferredAvatarAlphaProgram ("Deferred Avatar Alpha Shader"), which sets
// isDeferred+hasReflectionProbes+hasShadows but never
// mIndexedTextureChannels, so the [EXTRA_CODE_HERE] tex0.. block above is
// never injected for that shader and t5/s5 is genuinely free (matches the
// same free zone used for the indexed-texture case, see llshadermgr.cpp's
// comment on kIndexedTexRegisterBase).
#ifdef USE_DIFFUSE_TEX
Texture2D diffuseMap : register(t5);
SamplerState diffuseMapSampler : register(s5);
#endif

// env_mat is also declared (and actually used) by reflectionProbeF.hlsl -
// this copy was dead here (only ever referenced at its own declaration),
// same shape as diffuseMap above - deleted, not guarded.

// classic_mode is also declared by deferredUtil.hlsl/atmosphericsFuncs.hlsl
// (already guarded there) - genuinely used in main() below (this was
// wrongly deemed dead earlier against the placeholder main() body; the
// real ported body below does use it).
#ifndef LL_CLASSIC_MODE_DECLARED
#define LL_CLASSIC_MODE_DECLARED
uniform int classic_mode;
#endif

// sun_dir/moon_dir are also declared (and used, see calcPointLightOrSpotLight's
// caller below) by shadowUtil.hlsl/softenLightF.hlsl - genuinely dual-use,
// reuse the existing guard.
#ifndef LL_SUN_MOON_DIR_DECLARED
#define LL_SUN_MOON_DIR_DECLARED
uniform float3 sun_dir;
uniform float3 moon_dir;
#endif

// proj_mat/inv_proj/screen_res are also declared by deferredUtil.hlsl
// (already guarded there), but none of the three have any other reference
// anywhere in this file - confirmed via grep - dead copy-paste leftovers,
// same shape as this session's other cleanups. Deleting is safe here
// (unlike blurLightF.hlsl's inv_proj/screen_res, nothing this file calls
// needs any of these internally - deferredUtil.hlsl itself is attached
// later and keeps its own real, guarded declarations).
uniform float minimum_alpha;

// sun_up_factor is also declared by atmosphericsFuncs.hlsl/shadowUtil.hlsl
// (guarded there) - genuinely used below (light_dir selection), reuse the
// existing guard.
#ifndef LL_SUN_UP_FACTOR_DECLARED
#define LL_SUN_UP_FACTOR_DECLARED
uniform int sun_up_factor;
#endif
uniform float4 light_position[8];
uniform float3 light_direction[8];
uniform float4 light_attenuation[8];
uniform float3 light_diffuse[8];

void waterClip(float3 pos);
float3 srgb_to_linear(float3 c);
float3 linear_to_srgb(float3 c);
float4 applySkyAndWaterFog(float3 pos, float3 additive, float3 atten, float4 color);
void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen);
#endif

float getAmbientClamp();
void mirrorClip(float3 pos);

void sampleReflectionProbesLegacy(inout float3 ambenv, inout float3 glossenv, inout float3 legacyenv,
    float2 tc, float3 pos, float3 norm, float glossiness, float envIntensity, bool transparent, float3 amblit_linear);

// S24 (task #155, 2026-08-18): was a crude, non-matching simplification -
// wrong distance-attenuation curve (naive inverted_la/dist instead of the
// real dist_atten formula below), and completely missing the `diffuse`
// color multiply (returned raw `da * light_col`, so local-light contribution
// on any alpha-blended surface - hair, alpha clothing, alpha sculpts -
// ignored the surface's own texture color entirely and used a different
// falloff shape than every other lit surface in the scene). Re-ported
// faithfully from alphaF.glsl's real body - the class3/deferred/
// materialF.hlsl version of this same function (legacy materials) was
// already a correct, careful port and served as a working reference for
// this fix; the spec/glare terms there don't apply here (this file's own
// GLSL never had them - "no spec for alpha shader...").
float3 calcPointLightOrSpotLight(float3 light_col, float3 diffuse, float3 v, float3 n, float4 lp, float3 ln, float la, float fa, float is_pointlight, float ambiance)
{
    // SL-14895 inverted attenuation work-around
    // This routine is tweaked to match deferred lighting, but previously used an inverted la value. To reconstruct
    // that previous value now that the inversion is corrected, we reverse the calculations in LLPipeline::setupHWLights()
    // to recover the `adjusted_radius` value previously being sent as la.
    float falloff_factor = (12.0 * fa) - 9.0;
    float inverted_la = falloff_factor / la;

    float3 col = float3(0, 0, 0);

    float3 lv = lp.xyz - v;
    float dist = length(lv);
    float da = 1.0;

    if (dist > 0.0 && inverted_la > 0.0)
    {
        dist /= inverted_la;

        lv = normalize(lv);

        float dist_atten = clamp(1.0 - (dist - 1.0 * (1.0 - fa)) / fa, 0.0, 1.0);
        dist_atten *= dist_atten;
        dist_atten *= 2.0f;

        if (dist_atten > 0.0)
        {
            float spot = max(dot(-ln, lv), is_pointlight);
            da *= spot * spot; // GL_SPOT_EXPONENT=2

            da *= dot(n, lv);
            da = max(0.0, da);

            float lit = 0.0f;

            float amb_da = 0.0; //ambiance;
            if (da > 0)
            {
                lit = clamp(da * dist_atten, 0.0, 1.0);
                col = lit * light_col * diffuse;
                amb_da += (da * 0.5 + 0.5) * ambiance;
            }
            amb_da += (da * da * 0.5 + 0.5) * ambiance;
            amb_da *= dist_atten;
            amb_da = min(amb_da, 1.0f - lit);

            // SL-10969 ... need to work out why this blows out in many setups...
            //col.rgb += amb_da * light_col * diffuse;

            // no spec for alpha shader...
        }
    }
    float final_scale = 1.0;
    if (classic_mode > 0)
        final_scale = 0.9;
    col = max(col * final_scale, float3(0, 0, 0));
    return col;
}

struct PSInput
{
    // S24 (2026-08-02): field order must match alphaV.hlsl's VSOutput
    // exactly - confirmed via D3D11 debug-layer VS/PS linkage error
    // (id=343, "Semantic 'TEXCOORD' is defined for mismatched hardware
    // registers"). vertex_color was declared last here but the VS declares
    // it between vary_position and vary_texcoord0 (when USE_VERTEX_COLOR
    // is set) - that shift moved every register after it out of alignment.
    float4 position : SV_Position;

    float3 vary_fragcoord : TEXCOORD0;
    float3 vary_position : TEXCOORD1;
#ifdef USE_VERTEX_COLOR
    float4 vertex_color : COLOR0;
#endif
    float2 vary_texcoord0 : TEXCOORD2;
    float3 vary_norm : TEXCOORD3;
#ifdef HAS_DIFFUSE_LOOKUP
    nointerpolation int vary_texture_index : VARYTEXTUREINDEX;
#endif
};

float4 main(PSInput IN) : SV_Target
{
    // S24 (2026-08-28, task #225): was missing entirely - GLSL's flat in
    // int vary_texture_index is a real cross-stage varying, automatically
    // linked by name with no code needed on the fragment side; HLSL has no
    // such linkage; every field must be explicitly copied from PSInput.
    // Without this, the static int vary_texture_index diffuseLookup()'s
    // generated switch() reads (llshadermgr.cpp, texture_index_channels>1
    // case) was never assigned - HLSL zero-initializes an unassigned
    // static, so it silently stayed 0 for every pixel, on every face,
    // always sampling tex0 regardless of which texture a given face was
    // actually supposed to use. Invisible whenever a batch only ever had
    // one texture at index 0 (the common case), but as soon as two
    // differently-textured faces sharing this shader (both set to real
    // Alpha Blending, not Alpha Masking - the only path that reaches this
    // exact file) got batched together, every pixel in the whole batch
    // showed whichever texture happened to land at index 0 - the "leaf
    // shows the trunk's bark texture" root cause.
#ifdef HAS_DIFFUSE_LOOKUP
    vary_texture_index = IN.vary_texture_index;
#endif

    mirrorClip(IN.vary_position);

    float2 frag = IN.vary_fragcoord.xy / IN.vary_fragcoord.z * 0.5 + 0.5;

    float4 pos = float4(IN.vary_position, 1.0);
#ifndef IS_AVATAR_SKIN
    // clip against water plane unless this is a legacy avatar skin
    waterClip(pos.xyz);
#endif
    float3 norm = IN.vary_norm;

    float shadow = 1.0f;

#ifdef HAS_SUN_SHADOW
    shadow = sampleDirectionalShadow(pos.xyz, norm.xyz, frag);
#endif

#ifdef USE_DIFFUSE_TEX
    float4 diffuse_tap = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);
#endif

#ifdef USE_INDEXED_TEX
    float4 diffuse_tap = diffuseLookup(IN.vary_texcoord0.xy);
#endif

    float4 diffuse_srgb = diffuse_tap;

#ifdef FOR_IMPOSTOR
    float4 color;
    color.rgb = diffuse_srgb.rgb;
    color.a = 1.0;

    float final_alpha = diffuse_srgb.a * IN.vertex_color.a;
    diffuse_srgb.rgb *= IN.vertex_color.rgb;

    // Insure we don't pollute depth with invis pixels in impostor rendering
    if (final_alpha < minimum_alpha)
    {
        discard;
    }

    color.rgb = diffuse_srgb.rgb;
    color.a = final_alpha;

#else // FOR_IMPOSTOR

    float4 diffuse_linear = float4(srgb_to_linear(diffuse_srgb.rgb), diffuse_srgb.a);

    float3 light_dir = (sun_up_factor == 1) ? sun_dir : moon_dir; // TODO -- factor out "sun_up_factor" and just send in the appropriate light vector

    float final_alpha = diffuse_linear.a;

#ifdef IS_AVATAR_SKIN
    if (final_alpha < minimum_alpha)
    {
        discard;
    }
#endif

#ifdef USE_VERTEX_COLOR
    final_alpha *= IN.vertex_color.a;

    if (final_alpha < minimum_alpha)
    { // TODO: figure out how to get invisible faces out of
        // render batches without breaking glow
        discard;
    }

    diffuse_srgb.rgb *= IN.vertex_color.rgb;
    diffuse_linear.rgb = srgb_to_linear(diffuse_srgb.rgb);
#endif // USE_VERTEX_COLOR

    float3 sunlit;
    float3 amblit;
    float3 additive;
    float3 atten;

    calcAtmosphericVarsLinear(pos.xyz, norm, light_dir, sunlit, amblit, additive, atten);
    if (classic_mode > 0)
        sunlit *= 1.35;
    float3 sunlit_linear = sunlit;
    float3 amblit_linear = amblit;

    float3 irradiance = amblit;
    float3 glossenv = float3(0, 0, 0);
    float3 legacyenv = float3(0, 0, 0);
    sampleReflectionProbesLegacy(irradiance, glossenv, legacyenv, frag, pos.xyz, norm.xyz, 0.0, 0.0, true, amblit_linear);

    float da = dot(norm.xyz, light_dir.xyz);
    da = clamp(da, -1.0, 1.0);

    float final_da = da;
    final_da = clamp(final_da, 0.0f, 1.0f);

    float4 color = float4(0.0, 0.0, 0.0, 0.0);

    color.a = final_alpha;

    color.rgb = irradiance;
    if (classic_mode > 0)
    {
        final_da = pow(final_da, 1.2);
        float3 sun_contrib = float3(min(final_da, shadow), min(final_da, shadow), min(final_da, shadow));

        color.rgb = srgb_to_linear(color.rgb * 0.9 + linear_to_srgb(sun_contrib) * sunlit_linear * 0.7);
        sunlit_linear = srgb_to_linear(sunlit_linear);
    }
    else
    {
        float3 sun_contrib = min(final_da, shadow) * sunlit_linear;
        color.rgb += sun_contrib;
    }

    color.rgb *= diffuse_linear.rgb;

    float4 light = float4(0, 0, 0, 0);

    light.rgb += calcPointLightOrSpotLight(light_diffuse[1].rgb, diffuse_linear.rgb, pos.xyz, norm, light_position[1], light_direction[1].xyz, light_attenuation[1].x, light_attenuation[1].y, light_attenuation[1].z, light_attenuation[1].w);
    light.rgb += calcPointLightOrSpotLight(light_diffuse[2].rgb, diffuse_linear.rgb, pos.xyz, norm, light_position[2], light_direction[2].xyz, light_attenuation[2].x, light_attenuation[2].y, light_attenuation[2].z, light_attenuation[2].w);
    light.rgb += calcPointLightOrSpotLight(light_diffuse[3].rgb, diffuse_linear.rgb, pos.xyz, norm, light_position[3], light_direction[3].xyz, light_attenuation[3].x, light_attenuation[3].y, light_attenuation[3].z, light_attenuation[3].w);
    light.rgb += calcPointLightOrSpotLight(light_diffuse[4].rgb, diffuse_linear.rgb, pos.xyz, norm, light_position[4], light_direction[4].xyz, light_attenuation[4].x, light_attenuation[4].y, light_attenuation[4].z, light_attenuation[4].w);
    light.rgb += calcPointLightOrSpotLight(light_diffuse[5].rgb, diffuse_linear.rgb, pos.xyz, norm, light_position[5], light_direction[5].xyz, light_attenuation[5].x, light_attenuation[5].y, light_attenuation[5].z, light_attenuation[5].w);
    light.rgb += calcPointLightOrSpotLight(light_diffuse[6].rgb, diffuse_linear.rgb, pos.xyz, norm, light_position[6], light_direction[6].xyz, light_attenuation[6].x, light_attenuation[6].y, light_attenuation[6].z, light_attenuation[6].w);
    light.rgb += calcPointLightOrSpotLight(light_diffuse[7].rgb, diffuse_linear.rgb, pos.xyz, norm, light_position[7], light_direction[7].xyz, light_attenuation[7].x, light_attenuation[7].y, light_attenuation[7].z, light_attenuation[7].w);

    // sum local light contrib in linear colorspace
    color.rgb += light.rgb;

    color.rgb = applySkyAndWaterFog(pos.xyz, additive, atten, color).rgb;

#endif // #else // FOR_IMPOSTOR
    float final_scale = 1;
    if (classic_mode > 0)
        final_scale = 1.1;
#ifdef IS_HUD
    color.rgb = linear_to_srgb(color.rgb);
    final_scale = 1;
#endif

    color.rgb *= final_scale;
    return max(color, float4(0, 0, 0, 0));
}
