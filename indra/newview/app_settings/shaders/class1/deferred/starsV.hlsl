/**
 * @file class1/deferred/starsV.hlsl
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

uniform float4x4 texture_matrix0;
uniform float4x4 modelview_projection_matrix;
uniform float time;

struct VSInput
{
    float3 position : POSITION;
    float4 diffuse_color : COLOR0;
    float2 texcoord0 : TEXCOORD0;
};

#include "varying/starsVarying.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
    StarsVarying varying;
};

// S24 (2026-08-29, task #279 "RENDER WOW"): cheap deterministic float3->float
// hash (classic sin/frac trick - no HLSL builtin noise() the way old GLSL
// briefly had one). Seeded from IN.diffuse_color.rgb rather than IN.position
// - LLVOWLSky::updateStarGeometry() (newview/llvowlsky.cpp) writes the SAME
// mStarColors[vtx] value to all 6 vertices of one star's billboard quad
// (2 triangles), but IN.position differs per corner (each corner is the
// star's center +/- an up/left billboard offset) - hashing position would
// give each of the 4 corners of the SAME star a different seed, visibly
// desyncing color/twinkle across one star's own quad. vertex_color is the
// only per-vertex input that's genuinely constant across a whole star.
float starHash(float3 seed)
{
    float n = dot(seed, float3(12.9898, 78.233, 37.719));
    return frac(sin(n) * 43758.5453123);
}

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    float4 pos = mul(modelview_projection_matrix, float4(IN.position, 1.0));

    // smash to far clip plane to
    // avoid rendering on top of moon (do NOT write to gl_FragDepth, it's slow)
    pos.z = pos.w;

    OUT.position = pos;

    float t = fmod(time, 1.25f);
    OUT.varying.screenpos = IN.position.xy * float2(t, t);
    OUT.varying.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;
    OUT.varying.vertex_color = IN.diffuse_color;

    OUT.varying.star_seed = starHash(IN.diffuse_color.rgb);

    // S24 (task #279): soft Milky-Way-style stardust band - brightest along
    // a fixed great circle across the sky dome, fading with angular
    // distance. IN.position is close enough to the star's true center
    // direction (the billboard offset is tiny relative to DISTANCE_TO_STARS,
    // llvowlsky.cpp's dome radius) that using it directly per-corner is
    // visually smooth, unlike star_seed above which needs to be EXACTLY
    // stable per corner.
    float3 galactic_normal = normalize(float3(0.35, 0.15, 0.92));
    float band_dist = dot(normalize(IN.position), galactic_normal);
    OUT.varying.galactic_band = 1.0 - smoothstep(0.0, 0.35, abs(band_dist));

    return OUT;
}
