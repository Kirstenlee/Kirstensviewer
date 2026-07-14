/**
 * @file class1/deferred/exposureF.hlsl
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

Texture2D emissiveRect : register(t0);
SamplerState emissiveRectSampler : register(s0);
#ifdef USE_LAST_EXPOSURE
Texture2D exposureMap : register(t1);
SamplerState exposureMapSampler : register(s1);
#endif

uniform float dt;
uniform float2 noiseVec;

uniform float4 dynamic_exposure_params;
uniform float4 dynamic_exposure_params2;

float lum(float3 col)
{
    float3 l = float3(0.2126, 0.7152, 0.0722);
    return dot(l, col);
}

float4 main() : SV_Target
{
    float2 tc = float2(0.5, 0.5);

    float L = emissiveRect.SampleLevel(emissiveRectSampler, tc, 8).r;
    float max_L = dynamic_exposure_params.x;
    L = clamp(L, 0.0, max_L);
    L /= max_L;
    L = pow(L, 2.0);
    float s = lerp(dynamic_exposure_params.z, dynamic_exposure_params.y, L);
#ifdef USE_LAST_EXPOSURE
    float prev = exposureMap.Sample(exposureMapSampler, float2(0.5, 0.5)).r;

    float speed = -log(dynamic_exposure_params.w) / dynamic_exposure_params2.w;
    s = lerp(prev, s, 1 - exp(-speed * dt));
#endif

    return max(float4(s, s, s, dt), float4(0.0, 0.0, 0.0, 0.0));
}
