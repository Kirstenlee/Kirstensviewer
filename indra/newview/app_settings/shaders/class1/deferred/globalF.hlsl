/**
 * @file class1/deferred/globalF.hlsl
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


 // Global helper functions included in every fragment shader
 // DO NOT declare sampler uniforms here as OS X doesn't compile
 // them out

uniform float mirror_flag;
uniform float4 clipPlane;
uniform float clipSign;

void mirrorClip(float3 pos)
{
    if (mirror_flag > 0)
    {
        if ((dot(pos.xyz, clipPlane.xyz) + clipPlane.w) < 0.0)
        {
                discard;
        }
    }
}

float4 encodeNormal(float3 n, float env, float gbuffer_flag)
{
    float f = sqrt(8 * n.z + 8);
    return float4(n.xy / f + 0.5, env, gbuffer_flag);
}

float4 decodeNormal(float4 norm)
{
    float2 fenc = norm.xy * 4 - 2;
    float f = dot(fenc, fenc);
    float g = sqrt(1 - f / 4);
    float4 n;
    n.xy = fenc * g;
    n.z = 1 - f / 2;
    return n;
}

// 4x4 Bayer dither matrix normalized to [0, 1]
static const float BAYER_PATTERN[16] = {
     0.0 / 16.0,  8.0 / 16.0,  2.0 / 16.0, 10.0 / 16.0,
    12.0 / 16.0,  4.0 / 16.0, 14.0 / 16.0,  6.0 / 16.0,
     3.0 / 16.0, 11.0 / 16.0,  1.0 / 16.0,  9.0 / 16.0,
    15.0 / 16.0,  7.0 / 16.0, 13.0 / 16.0,  5.0 / 16.0
};

// Discard fragment based on dithered alpha threshold.
// Fragments with alpha < 0.05 are always discarded.
// Fragments with alpha >= threshold are always kept.
// Fragments in between are dithered using a 4x4 Bayer pattern.
void bayerDitherDiscard(float alpha, float threshold, float4 fragCoord)
{
    if (alpha < 0.05)
    {
        discard;
    }

    if (alpha < threshold)
    {
        int2 ipos = int2(fmod(fragCoord.xy, 4.0));
        if (alpha < BAYER_PATTERN[ipos.y * 4 + ipos.x])
        {
            discard;
        }
    }
}
