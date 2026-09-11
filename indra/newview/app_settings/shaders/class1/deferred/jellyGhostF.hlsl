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

// S24 (2026-09-10): jelly-doll "ghost" rendering. Replaces the old opaque
// flat-grey impostor look with a translucent, rim-glowing silhouette -
// same cached impostor texture, same zero extra geometry/texture cost,
// drawn through LLDrawPoolAvatar's existing post-deferred avatar-alpha
// pass (real alpha blend, composited against the already-lit scene -
// see LLDrawPoolAvatar::renderJellyDollGhosts()) instead of the opaque
// G-buffer impostor pass.
//
// Deliberately does NOT sample the impostor's baked RGB - only its ALPHA
// channel is used, as a silhouette shape mask (the bake's color-stomp pass,
// LLPipeline::generateImpostor(), still writes a real per-pixel alpha built
// from a depth test against the avatar's true silhouette - see that
// function's own comment). This shader supplies its own color entirely
// from uniforms instead, so the old baked RGB value is irrelevant here.
//
// Edge/rim glow: a billboarded impostor quad has no real 3D surface normal
// to run an actual fresnel (dot(N,V)) term against - the standard, cheap
// substitute for a flat sprite/billboard is to use the SCREEN-SPACE
// GRADIENT of the alpha mask itself: deep inside the silhouette the mask is
// flat (~1, zero gradient); right at the silhouette's edge it transitions
// sharply (large gradient). ddx()/ddy() give that gradient for free, no
// extra texture samples needed.

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

    // S24 (2026-09-10): rim MUST be clamped to [0,1] before it drives either
    // output - the original version saturated the raw gradient and then
    // multiplied by jelly_rim_intensity *after*, so rim could exceed 1.0
    // (e.g. 1.2 at the default intensity). That pushed rgb past white
    // (clipping to a hard pure-white outline) and alpha to fully opaque
    // across the whole gradient band (not just a thin edge, since a
    // filtered/mip-mapped impostor alpha edge is several texels wide, not
    // a single pixel) - live-tested result was a solid black cutout with a
    // thick jagged white sticker-outline, not the intended gentle
    // translucent glow. smoothstep() softens the [0,1] response into a
    // gradual S-curve instead of a hard threshold, killing the jaggedness.
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
