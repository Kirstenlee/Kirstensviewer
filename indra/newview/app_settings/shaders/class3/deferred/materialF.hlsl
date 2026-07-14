/**
 * @file class3/deferred/materialF.hlsl
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

//class1/deferred/materialF.glsl

// This shader is used for both writing opaque/masked content to the gbuffer and writing blended content to the framebuffer during the alpha pass.

#define DIFFUSE_ALPHA_MODE_NONE     0
#define DIFFUSE_ALPHA_MODE_BLEND    1
#define DIFFUSE_ALPHA_MODE_MASK     2
#define DIFFUSE_ALPHA_MODE_EMISSIVE 3

uniform float emissive_brightness;  // fullbright flag, 1.0 == fullbright, 0.0 otherwise
uniform int sun_up_factor;
uniform int classic_mode;

float4 applySkyAndWaterFog(float3 pos, float3 additive, float3 atten, float4 color);
float3 scaleSoftClipFragLinear(float3 l);
void calcAtmosphericVarsLinear(float3 inPositionEye, float3 norm, float3 light_dir, out float3 sunlit, out float3 amblit, out float3 atten, out float3 additive);
void calcHalfVectors(float3 lv, float3 n, float3 v, out float3 h, out float3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);

float3 srgb_to_linear(float3 cs);
float3 linear_to_srgb(float3 cs);

uniform float4x4 modelview_matrix;
uniform float3x3 normal_matrix;

void mirrorClip(float3 pos);
float4 encodeNormal(float3 n, float env, float gbuffer_flag);

struct PSInput
{
    float3 vary_position : TEXCOORD0;
#ifdef HAS_NORMAL_MAP
    float3 vary_tangent : TEXCOORD1;
    nointerpolation float vary_sign : TEXCOORD2;
    float3 vary_normal : TEXCOORD3;
    float2 vary_texcoord1 : TEXCOORD4;
#else
    float3 vary_normal : TEXCOORD1;
#endif
#ifdef HAS_SPECULAR_MAP
    float2 vary_texcoord2 : TEXCOORD5;
#endif
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD6;
};

#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)

struct PSOutput
{
    float4 frag_color : SV_Target;
};

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(float3 pos, float3 norm, float2 pos_screen);
#endif

void sampleReflectionProbesLegacy(inout float3 ambenv, inout float3 glossenv, inout float3 legacyenv,
        float2 tc, float3 pos, float3 norm, float glossiness, float envIntensity, bool transparent, float3 amblit_linear);
void applyGlossEnv(inout float3 color, float3 glossenv, float4 spec, float3 pos, float3 norm);
void applyLegacyEnv(inout float3 color, float3 legacyenv, float4 spec, float3 pos, float3 norm, float envIntensity);

TextureCube environmentMap : register(t4);
SamplerState environmentMapSampler : register(s4);
Texture2D lightFunc : register(t5);
SamplerState lightFuncSampler : register(s5);

// Inputs
uniform float4 morphFactor;
uniform float3 camPosLocal;
uniform float3x3 env_mat;

uniform float is_mirror;

uniform float3 sun_dir;
uniform float3 moon_dir;

uniform float4x4 proj_mat;
uniform float4x4 inv_proj;
uniform float2 screen_res;

uniform float4 light_position[8];
uniform float3 light_direction[8];
uniform float4 light_attenuation[8];
uniform float3 light_diffuse[8];

float getAmbientClamp();
void waterClip(float3 pos);

float3 calcPointLightOrSpotLight(float3 light_col, float3 npos, float3 diffuse, float4 spec, float3 v, float3 n, float4 lp, float3 ln, float la, float fa, float is_pointlight, inout float glare, float ambiance)
{
    // SL-14895 inverted attenuation work-around
    // This routine is tweaked to match deferred lighting, but previously used an inverted la value. To reconstruct
    // that previous value now that the inversion is corrected, we reverse the calculations in LLPipeline::setupHWLights()
    // to recover the `adjusted_radius` value previously being sent as la.
    float falloff_factor = (12.0 * fa) - 9.0;
    float inverted_la = falloff_factor / la;
    // Yes, it makes me want to cry as well. DJH

    float3 col = float3(0, 0, 0);

    //get light vector
    float3 lv = lp.xyz - v;

    //get distance
    float dist = length(lv);
    float da = 1.0;

    dist /= inverted_la;

    if (dist > 0.0 && inverted_la > 0.0)
    {
        //normalize light vector
        lv = normalize(lv);

        //distance attenuation
        float dist_atten = clamp(1.0 - (dist - 1.0*(1.0 - fa)) / fa, 0.0, 1.0);
        dist_atten *= dist_atten;
        dist_atten *= 2.0f;

        if (dist_atten <= 0.0)
        {
            return col;
        }

        // spotlight coefficient.
        float spot = max(dot(-ln, lv), is_pointlight);
        da *= spot*spot; // GL_SPOT_EXPONENT=2

        //angular attenuation
        da *= dot(n, lv);

        float lit = 0.0f;

        float amb_da = ambiance;
        if (da >= 0)
        {
            lit = clamp(da * dist_atten, 0.0, 1.0);
            col = lit * light_col * diffuse;
            amb_da += (da*0.5 + 0.5) * ambiance;
        }
        amb_da += (da*da*0.5 + 0.5) * ambiance;
        amb_da *= dist_atten;
        amb_da = min(amb_da, 1.0f - lit);

        // SL-10969 need to see why these are blown out
        //col.rgb += amb_da * light_col * diffuse;

        if (spec.a > 0.0)
        {
            //float3 ref = dot(pos+lv, norm);
            float3 h = normalize(lv + npos);
            float nh = dot(n, h);
            float nv = dot(n, npos);
            float vh = dot(npos, h);
            float sa = nh;
            float fres = pow(1 - dot(h, npos), 5)*0.4 + 0.5;

            float gtdenom = 2 * nh;
            float gt = max(0, min(gtdenom * nv / vh, gtdenom * da / vh));

            if (nh > 0.0)
            {
                float scol = fres*lightFunc.Sample(lightFuncSampler, float2(nh, spec.a)).r*gt / (nh*da);
                float3 speccol = lit*scol*light_col.rgb*spec.rgb;
                speccol = clamp(speccol, float3(0, 0, 0), float3(1, 1, 1));
                col += speccol;

                float cur_glare = max(speccol.r, speccol.g);
                cur_glare = max(cur_glare, speccol.b);
                glare = max(glare, speccol.r);
                glare += max(cur_glare, 0.0);
            }
        }
    }
    float final_scale = 1.0;
    if (classic_mode > 0)
        final_scale = 0.9;
    return max(col * final_scale, float3(0.0, 0.0, 0.0));
}

#else
struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};
#endif

Texture2D diffuseMap : register(t0);  //always in sRGB space
SamplerState diffuseMapSampler : register(s0);

#ifdef HAS_NORMAL_MAP
Texture2D bumpMap : register(t1);
SamplerState bumpMapSampler : register(s1);
#endif

#ifdef HAS_SPECULAR_MAP
Texture2D specularMap : register(t2);
SamplerState specularMapSampler : register(s2);
#endif

uniform float env_intensity;
uniform float4 specular_color;  // specular color RGB and specular exponent (glossiness) in alpha

#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_MASK)
uniform float minimum_alpha;
#endif

// get the transformed normal and apply glossiness component from normal map
float3 getNormal(inout float glossiness, PSInput IN)
{
#ifdef HAS_NORMAL_MAP
    float4 vNt = bumpMap.Sample(bumpMapSampler, IN.vary_texcoord1.xy);
    glossiness *= vNt.a;
    vNt.xyz = vNt.xyz * 2 - 1;
    float sign_ = IN.vary_sign;
    float3 vN = IN.vary_normal;
    float3 vT = IN.vary_tangent.xyz;

    float3 vB = sign_ * cross(vN, vT);
    float3 tnorm = normalize( vNt.x * vT + vNt.y * vB + vNt.z * vN );

    return tnorm;
#else
    return normalize(IN.vary_normal);
#endif
}

float4 getSpecular(PSInput IN)
{
#ifdef HAS_SPECULAR_MAP
    float4 spec = specularMap.Sample(specularMapSampler, IN.vary_texcoord2.xy);
    spec.rgb *= specular_color.rgb;
#else
    float4 spec = float4(specular_color.rgb, 1.0);
#endif
    return spec;
}

void alphaMask(float alpha)
{
#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_MASK)
    // Comparing floats cast from 8-bit values, produces acne right at the 8-bit transition points
    float bias = 0.001953125; // 1/512, or half an 8-bit quantization
    if (alpha < minimum_alpha-bias)
    {
        discard;
    }
#endif
}

void waterClip(PSInput IN)
{
#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)
    waterClip(IN.vary_position.xyz);
#endif
}

float getEmissive(float4 diffcol)
{
#if (DIFFUSE_ALPHA_MODE != DIFFUSE_ALPHA_MODE_EMISSIVE)
    return emissive_brightness;
#else
    return max(diffcol.a, emissive_brightness);
#endif
}

float getShadow(float3 pos, float3 norm, PSInput IN)
{
#ifdef HAS_SUN_SHADOW
    #if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)
        return sampleDirectionalShadow(pos, norm, IN.vary_texcoord0.xy);
    #else
        return 1;
    #endif
#else
    return 1;
#endif
}

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    mirrorClip(IN.vary_position);
    waterClip(IN);

    // diffcol == diffuse map combined with vertex color
    float4 diffcol = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);
    diffcol.rgb *= IN.vertex_color.rgb;
    alphaMask(diffcol.a);

    // spec == specular map combined with specular color
    float4 spec = getSpecular(IN);
    float env = env_intensity * spec.a;
    float glossiness = specular_color.a;
    float3 norm = getNormal(glossiness, IN);

    float emissive = getEmissive(diffcol);

#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)
    //forward rendering, output lit linear color
    diffcol.rgb = srgb_to_linear(diffcol.rgb);
    spec.rgb = srgb_to_linear(spec.rgb);
    spec.a = glossiness; // pack glossiness into spec alpha for lighting functions

    float3 pos = IN.vary_position;

    float shadow = getShadow(pos, norm, IN);

    float4 diffuse = diffcol;

    float3 color = float3(0, 0, 0);

    float3 light_dir = (sun_up_factor == 1) ? sun_dir : moon_dir;

    float bloom = 0.0;
    float3 sunlit;
    float3 amblit;
    float3 additive;
    float3 atten;
    calcAtmosphericVarsLinear(pos.xyz, norm.xyz, light_dir, sunlit, amblit, additive, atten);
    if (classic_mode > 0)
        sunlit *= 1.35;
    float3 sunlit_linear = sunlit;
    float3 amblit_linear = amblit;

    float3 ambenv = amblit;
    float3 glossenv;
    float3 legacyenv;
    sampleReflectionProbesLegacy(ambenv, glossenv, legacyenv, pos.xy*0.5+0.5, pos.xyz, norm.xyz, glossiness, env, true, amblit_linear);

    color = ambenv;

    float da          = clamp(dot(norm.xyz, light_dir.xyz), 0.0, 1.0);
    if (classic_mode > 0)
    {
        da = pow(da,1.2);
        float3 sun_contrib = float3(min(da, shadow), min(da, shadow), min(da, shadow));

        color.rgb = srgb_to_linear(color.rgb * 0.9 + linear_to_srgb(sun_contrib) * sunlit_linear * 0.7);
        sunlit_linear = srgb_to_linear(sunlit_linear);
    }
    else
    {
        float3 sun_contrib = min(da, shadow) * sunlit_linear;
        color.rgb += sun_contrib;
    }

    color *= diffcol.rgb;

    float3 refnormpersp = reflect(pos.xyz, norm.xyz);

    float glare = 0.0;

    if (glossiness > 0.0)
    {
        float3  lv = light_dir.xyz;
        float3  h, l, v = -normalize(pos.xyz);
        float nh, nl, nv, vh, lightDist;
        float3 n = norm.xyz;
        calcHalfVectors(lv, n, v, h, l, nh, nl, nv, vh, lightDist);

        if (nl > 0.0 && nh > 0.0)
        {
            float lit = min(nl*6.0, 1.0);

            float sa = nh;
            float fres = pow(1 - vh, 5) * 0.4+0.5;
            float gtdenom = 2 * nh;
            float gt = max(0,(min(gtdenom * nv / vh, gtdenom * nl / vh)));

            float scol = shadow*fres*lightFunc.Sample(lightFuncSampler, float2(nh, glossiness)).r*gt/(nh*nl);
            color.rgb += lit*scol*sunlit_linear.rgb*spec.rgb;
        }

        // add radiance map
        applyGlossEnv(color, glossenv, spec, pos.xyz, norm.xyz);
    }

    color = lerp(color.rgb, diffcol.rgb, emissive);

    if (env > 0.0)
    {  // add environmentmap
        applyLegacyEnv(color, legacyenv, spec, pos.xyz, norm.xyz, env);

        float cur_glare = max(max(legacyenv.r, legacyenv.g), legacyenv.b);
        cur_glare = clamp(cur_glare, 0, 1);
        cur_glare *= env;
        glare += cur_glare;
    }

    float3 npos = normalize(-pos.xyz);
    float3 light = float3(0, 0, 0);

#define LIGHT_LOOP(i) light.rgb += calcPointLightOrSpotLight(light_diffuse[i].rgb, npos, diffuse.rgb, spec, pos.xyz, norm.xyz, light_position[i], light_direction[i].xyz, light_attenuation[i].x, light_attenuation[i].y, light_attenuation[i].z, glare, light_attenuation[i].w );

    LIGHT_LOOP(1)
        LIGHT_LOOP(2)
        LIGHT_LOOP(3)
        LIGHT_LOOP(4)
        LIGHT_LOOP(5)
        LIGHT_LOOP(6)
        LIGHT_LOOP(7)

    color += light;

    color.rgb = applySkyAndWaterFog(pos.xyz, additive, atten, float4(color, 1.0)).rgb;

    glare *= 1.0-emissive;
    glare = min(glare, 1.0);
    float al = max(diffcol.a, glare) * IN.vertex_color.a;
    float final_scale = 1;
    if (classic_mode > 0)
        final_scale = 1.1;
    OUT.frag_color = max(float4(color * final_scale, al), float4(0, 0, 0, 0));

#else // mode is not DIFFUSE_ALPHA_MODE_BLEND, encode to gbuffer
    // deferred path               // See: C++: addDeferredAttachment(), shader: softenLightF.hlsl

    float flag = GBUFFER_FLAG_HAS_ATMOS;

    OUT.data0 = max(float4(diffcol.rgb, emissive), float4(0, 0, 0, 0));        // gbuffer is sRGB for legacy materials
    OUT.data1 = max(float4(spec.rgb, glossiness), float4(0, 0, 0, 0));           // XYZ = Specular color. W = Specular exponent.
    OUT.data2 = encodeNormal(norm, env, flag);   // XY = Normal.  Z = Env. intensity. W = 1 skip atmos (mask off fog)

#if defined(HAS_EMISSIVE)
    OUT.data3 = float4(0, 0, 0, 0);
#endif

#endif

    return OUT;
}
