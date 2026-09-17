/**
 * @file class1/deferred/jellyGhostF.hlsl
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

// Jelly-doll "ghost" impostor: drawn through LLDrawPoolAvatar's post-deferred
// avatar-alpha pass (real alpha blend against the already-lit scene - see
// LLDrawPoolAvatar::renderJellyDollGhosts()), not the opaque G-buffer
// impostor pass.
//
// Only the impostor's ALPHA channel is sampled, as a silhouette mask - its
// baked RGB is unused; color comes entirely from the uniforms below.
//
// Rim glow: a billboarded impostor quad has no real surface normal for a
// fresnel term, so the screen-space gradient of the alpha mask (ddx/ddy)
// stands in for it - flat inside the silhouette, sharp at its edge.

uniform float minimum_alpha;
uniform float3 jelly_base_color;
uniform float jelly_base_alpha;
uniform float3 jelly_rim_color;
uniform float jelly_rim_intensity;
uniform float jelly_rim_width;

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 vary_texcoord0 : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    float silhouette = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy).a;

    if (silhouette < minimum_alpha)
    {
        discard;
    }

    // rim must be clamped to [0,1] BEFORE jelly_rim_intensity multiplies it,
    // not after - clamping after intensity lets rim exceed 1.0, pushing rgb
    // to solid white and alpha to fully opaque across the whole (multi-texel,
    // filtered) edge band instead of a soft glow. smoothstep() avoids a hard
    // threshold, which would look jagged.
    float grad = length(float2(ddx(silhouette), ddy(silhouette)));
    float rim = smoothstep(0.0, 1.0, saturate(grad * jelly_rim_width));

    // Color: rim contribution is scaled by intensity AFTER clamping rim
    // itself, then the whole sum is saturated - can brighten the edge but
    // can never blow out past white.
    float3 rgb = saturate(jelly_base_color + jelly_rim_color * rim * jelly_rim_intensity);

    // Alpha: the rim must only ever nudge translucency up a little, never
    // slam the body to fully opaque - the body's visible transparency has
    // to stay dominated by jelly_base_alpha (the actual "80% transparent"
    // ask), with just a soft, modest brightening right at the silhouette
    // edge, not a hard opaque outline.
    float alpha = saturate(jelly_base_alpha + rim * jelly_rim_intensity * 0.1);

    return float4(rgb, alpha);
}
