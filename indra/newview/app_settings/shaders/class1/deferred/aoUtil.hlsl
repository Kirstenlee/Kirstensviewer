/**
 * @file class1/deferred/aoUtil.hlsl
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

uniform Texture2D noiseMap : register(t0);
uniform Texture2D depthMap : register(t1);
uniform SamplerState noiseSampler : register(s0);
uniform SamplerState depthSampler : register(s1);

uniform float ssao_radius;
uniform float ssao_max_radius;
uniform float ssao_factor;
uniform float ssao_factor_inv;

uniform float4x4 inv_proj;
uniform float2 screen_res;

float2 getScreenCoordinateAo(float2 screenpos)
{
    float2 sc = screenpos.xy * 2.0;
    return sc - float2(1.0, 1.0);
}

float getDepthAo(float2 pos_screen)
{
    float depth = depthMap.Sample(depthSampler, pos_screen).r;
    return depth;
}

float4 getPositionAo(float2 pos_screen)
{
    float depth = getDepthAo(pos_screen);
    float2 sc = getScreenCoordinateAo(pos_screen);
    float4 ndc = float4(sc.x, sc.y, 2.0 * depth - 1.0, 1.0);
    float4 pos = mul(inv_proj, ndc);
    pos /= pos.w;
    pos.w = 1.0;
    return pos;
}

float2 getKern(int i)
{
    float2 kern[8];
    // exponentially (^2) distant occlusion samples spread around origin
    kern[0] = float2(-1.0, 0.0) * 0.125 * 0.125;
    kern[1] = float2(1.0, 0.0) * 0.250 * 0.250;
    kern[2] = float2(0.0, 1.0) * 0.375 * 0.375;
    kern[3] = float2(0.0, -1.0) * 0.500 * 0.500;
    kern[4] = float2(0.7071, 0.7071) * 0.625 * 0.625;
    kern[5] = float2(-0.7071, -0.7071) * 0.750 * 0.750;
    kern[6] = float2(-0.7071, 0.7071) * 0.875 * 0.875;
    kern[7] = float2(0.7071, -0.7071) * 1.000 * 1.000;

    return kern[i] / screen_res;
}

// Calculate decreases in ambient lighting when crowded out (SSAO)
float calcAmbientOcclusion(float4 pos, float3 norm, float2 pos_screen)
{
    float ret = 1.0;
    float3 pos_world = pos.xyz;
    float2 noise_reflect = noiseMap.Sample(noiseSampler, pos_screen.xy * (screen_res / 128)).xy;

    float angle_hidden = 0.0;
    float points = 0;

    float scale = min(ssao_radius / -pos_world.z, ssao_max_radius);

    // it was found that keeping # of samples a constant was the fastest, probably due to compiler optimizations (unrolling?)
    for (int i = 0; i < 8; i++)
    {
        float2 samppos_screen = pos_screen + scale * reflect(getKern(i), noise_reflect);
        float3 samppos_world = getPositionAo(samppos_screen).xyz;

        float3 diff = pos_world - samppos_world;
        float dist2 = dot(diff, diff);

        // assume each sample corresponds to an occluding sphere with constant radius, constant x-sectional area
        // --> solid angle shrinking by the square of distance
        //radius is somewhat arbitrary, can approx with just some constant k * 1 / dist^2
        //(k should vary inversely with # of samples, but this is taken care of later)

        float funky_val = (dot((samppos_world - 0.05 * norm - pos_world), norm) > 0.0) ? 1.0 : 0.0;
        angle_hidden = angle_hidden + funky_val * min(1.0 / dist2, ssao_factor_inv);

        // 'blocked' samples (significantly closer to camera relative to pos_world) are "no data", not "no occlusion"
        float diffz_val = (diff.z > -1.0) ? 1.0 : 0.0;
        points = points + diffz_val;
    }

    angle_hidden = min(ssao_factor * angle_hidden / points, 1.0);

    float points_val = (points > 0.0) ? 1.0 : 0.0;
    ret = (1.0 - (points_val * angle_hidden));

    ret = max(ret, 0.0);
    return min(ret, 1.0);
}
