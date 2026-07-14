/**
 * @file class3/deferred/reflectionProbeF.hlsl
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

#define FLT_MAX 3.402823466e+38

#if defined(SSR)
float tapScreenSpaceReflection(int totalSamples, float2 tc, float3 viewPos, float3 n, inout float4 collectedColor, Texture2D source, SamplerState sourceSampler, float glossiness);
#endif

TextureCubeArray reflectionProbes : register(t0);
SamplerState reflectionProbesSampler : register(s0);
TextureCubeArray irradianceProbes : register(t1);
SamplerState irradianceProbesSampler : register(s1);

Texture2D sceneMap : register(t2);
SamplerState sceneMapSampler : register(s2);

uniform int cube_snapshot;
uniform float max_probe_lod;
uniform bool transparent_surface;
uniform int classic_mode;

#define MAX_REFMAP_COUNT 256

cbuffer ReflectionProbes : register(b0)
{
    float4x4 refBox[MAX_REFMAP_COUNT];
    float4x4 heroBox;
    float4 refSphere[MAX_REFMAP_COUNT];
    float4 refParams[MAX_REFMAP_COUNT];
    float4 heroSphere;
    int4 refIndex[MAX_REFMAP_COUNT];
    int4 refNeighbor[1024];
    int4 refBucket[256];
    int refmapCount;
    int heroShape;
    int heroMipCount;
    int heroProbeCount;
}

uniform float3x3 env_mat;

int probeIndex[REF_SAMPLE_COUNT];
int probeInfluences = 0;
bool sample_automatic = true;

struct PSInput
{
    float2 vary_fragcoord : TEXCOORD0;
};

bool isAbove(float3 pos, float4 plane)
{
    return (dot(plane.xyz, pos) + plane.w) > 0;
}

bool shouldSampleProbe(int i, float3 pos)
{
    if (refIndex[i].w < 0)
    {
        float4 v = mul(refBox[i], float4(pos, 1.0));
        if (abs(v.x) > 1 || abs(v.y) > 1 || abs(v.z) > 1)
            return false;
        sample_automatic = false;
    }
    else
    {
        if (refIndex[i].w == 0 && !sample_automatic)
            return false;
        float3 delta = pos.xyz - refSphere[i].xyz;
        return dot(delta, delta) < refSphere[i].w * refSphere[i].w;
    }
    return true;
}

float4 main(PSInput IN) : SV_Target
{
    // Placeholder: full reflection probe implementation
    return float4(0, 0, 0, 0);
}
