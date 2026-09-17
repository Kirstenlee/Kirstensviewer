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

// Cheap deterministic float3->float hash (sin/frac trick; no HLSL builtin
// noise()). Seeded from IN.diffuse_color.rgb rather than IN.position:
// LLVOWLSky::updateStarGeometry() writes the same mStarColors[vtx] value to
// all 6 vertices of one star's billboard quad, but IN.position differs per
// corner - hashing position would desync color/twinkle across one star's quad.
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
    // Reversed-Z: far plane is 0.0, not 1.0 (modelview_projection_matrix
    // already carries kGLtoDXDepthRemap, see llrender.cpp) - pos.z=pos.w
    // would smash to the near plane instead.
    pos.z = 0.0;

    OUT.position = pos;

    float t = fmod(time, 1.25f);
    OUT.varying.screenpos = IN.position.xy * float2(t, t);
    OUT.varying.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0, 0, 1)).xy;
    OUT.varying.vertex_color = IN.diffuse_color;

    OUT.varying.star_seed = starHash(IN.diffuse_color.rgb);

    return OUT;
}
