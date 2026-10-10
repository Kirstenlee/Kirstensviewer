/**
 * @file class3/deferred/alphaProjectorF.hlsl
 * @brief Projected-light term for alpha-pool surfaces (RenderAlphaProjectors).
 *
 * Forward additive pass, one draw per nearby spotlight projector. The output is weighted by the
 * surface alpha, so projected light fades with transparency instead of disappearing. Self-contained:
 * the projector maths is copied from deferredUtil.hlsl and spotLightF.hlsl and no deferred helpers
 * are attached to this program.
 */

struct PSInput
{
    float4 position : SV_Position;
    float3 vary_fragcoord : TEXCOORD0;
    float3 vary_position : TEXCOORD1;
    float2 vary_texcoord0 : TEXCOORD2;
    float3 vary_norm : TEXCOORD3;
};

uniform Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

uniform Texture2D projectionMap : register(t2);
SamplerState projectionMapSampler : register(s2);

uniform float4x4 proj_mat;
uniform float3 proj_n;
uniform float proj_focus;
uniform float proj_lod;
uniform float proj_range;

uniform float3 center;
uniform float size;
uniform float falloff;
uniform float3 color;

uniform float proj_object_alpha;

float3 projToLinear(float3 c)
{
    return pow(max(c, float3(0.0, 0.0, 0.0)), float3(2.2, 2.2, 2.2));
}

float4 sampleProjection(float2 tc, float lod)
{
    float4 ret = projectionMap.SampleLevel(projectionMapSampler, tc, lod);
    ret.rgb = projToLinear(ret.rgb);
    float2 dist = float2(0.5, 0.5) - abs(tc - float2(0.5, 0.5));
    float det = min(lod / (proj_lod * 0.5), 1.0);
    float d = min(dist.x, dist.y);
    ret *= clamp(d / (0.25 * det), 0.0, 1.0);
    return ret;
}

float3 projectedColor(float light_distance, float2 uv)
{
    float diff = clamp((light_distance - proj_focus) / proj_range, 0.0, 1.0);
    float4 plcol = sampleProjection(uv, diff * proj_lod);
    return color * plcol.rgb * plcol.a;
}

float legacyDistanceAttenuation(float d, float fall)
{
    float atten = 1.0 - clamp((d + fall) / (1.0 + fall), 0.0, 1.0);
    atten *= atten;
    atten *= 2.0;
    return atten;
}

float4 main(PSInput IN) : SV_Target
{
    float3 pos = IN.vary_position;
    float3 lv = center - pos;
    float dist = length(lv);
    if (dist >= size)
    {
        discard;
    }

    float4 proj = mul(proj_mat, float4(pos, 1.0));
    if (proj.z < 0.0 || proj.w <= 0.0)
    {
        discard;
    }
    proj.xyz /= proj.w;
    if (proj.x <= 0.0 || proj.x >= 1.0 || proj.y <= 0.0 || proj.y >= 1.0)
    {
        discard;
    }

    float l_dist = -dot(lv, proj_n);
    float dist_atten = legacyDistanceAttenuation(dist / size, falloff);

    float3 n = normalize(IN.vary_norm);
    float nl = max(dot(n, normalize(lv)), 0.0);

    float3 light = projectedColor(l_dist, proj.xy) * nl * dist_atten;

    // Surface alpha = texture alpha times the object's own alpha. Additive blend, so this is the
    // light added on top of what the alpha pass already drew.
    float a = proj_object_alpha * diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0).a;

    return float4(light * a, 0.0);
}
