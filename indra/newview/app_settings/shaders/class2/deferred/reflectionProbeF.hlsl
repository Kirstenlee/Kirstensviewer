/**
 * @file class2/deferred/reflectionProbeF.hlsl
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

// Implementation for when reflection probes are disabled

uniform float reflection_probe_ambiance;

TextureCube environmentMap : register(t0);
SamplerState environmentMapSampler : register(s0);

uniform float3x3 env_mat;

float3 srgb_to_linear(float3 c);
float3 linear_to_srgb(float3 c);

void sampleReflectionProbes(inout float3 ambenv, inout float3 glossenv,
        float2 tc, float3 pos, float3 norm, float glossiness, bool transparent, float3 amblit_linear)
{
    ambenv = lerp(ambenv, float3(reflection_probe_ambiance * 0.25, reflection_probe_ambiance * 0.25, reflection_probe_ambiance * 0.25), reflection_probe_ambiance);

    float3 refnormpersp = normalize(reflect(pos.xyz, norm.xyz));
    float3 env_vec = mul(env_mat, refnormpersp);
    glossenv = srgb_to_linear(environmentMap.Sample(environmentMapSampler, env_vec).rgb);
}

void sampleReflectionProbesWater(inout float3 ambenv, inout float3 glossenv,
        float2 tc, float3 pos, float3 norm, float glossiness, float3 amblit_linear)
{
    sampleReflectionProbes(ambenv, glossenv, tc, pos, norm, glossiness, false, amblit_linear);
}

float4 sampleReflectionProbesDebug(float3 pos)
{
    // show nothing in debug display
    return float4(0, 0, 0, 0);
}

void sampleReflectionProbesLegacy(inout float3 ambenv, inout float3 glossenv, inout float3 legacyenv,
        float2 tc, float3 pos, float3 norm, float glossiness, float envIntensity, bool transparent, float3 amblit_linear)
{
    ambenv = lerp(ambenv, float3(reflection_probe_ambiance * 0.25, reflection_probe_ambiance * 0.25, reflection_probe_ambiance * 0.25), reflection_probe_ambiance);

    float3 refnormpersp = normalize(reflect(pos.xyz, norm.xyz));
    float3 env_vec = mul(env_mat, refnormpersp);

    legacyenv = environmentMap.Sample(environmentMapSampler, env_vec).rgb;

    glossenv = legacyenv;
}

void applyGlossEnv(inout float3 color, float3 glossenv, float4 spec, float3 pos, float3 norm)
{

}

void applyLegacyEnv(inout float3 color, float3 legacyenv, float4 spec, float3 pos, float3 norm, float envIntensity)
{
    color = srgb_to_linear(lerp(linear_to_srgb(color.rgb), legacyenv*2.0, envIntensity));
}
