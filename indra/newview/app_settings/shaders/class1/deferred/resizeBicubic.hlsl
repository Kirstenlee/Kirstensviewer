/**
 * @file class1/deferred/resizeBicubic.hlsl
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

/*[EXTRA_CODE_HERE]*/

// S24 (2026-08-26, task #263): separable 4-tap Catmull-Rom (a=-0.5) bicubic
// resize pass - one shader, bound TWICE per resize (horizontal then
// vertical) with a different glowDelta each time, exactly the same reuse
// pattern gGlowProgram already uses for its own separable blur
// (LLPipeline::generateGlow(), pipeline.cpp) - not two separate shaders.
// LLGPUResize::resize() (newview/llgpuresize.cpp) is the only caller.
//
// t7/s7 matches postDeferredGammaCorrect.hlsl's established "free slot for
// a single post-fx input" convention. glowDelta is glow's own uniform
// (LLShaderMgr::GLOW_DELTA, "glowDelta") reused here rather than adding a
// new uniform name - same shape (a per-axis UV step, nonzero on exactly
// one axis per pass), just a different consumer.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

uniform float2 glowDelta;

// S24 (2026-08-26, task #263 round 6): blend the 4 taps in linear light,
// not on the raw gamma-encoded source (this shader's input is the final
// post-gamma-correct composited target) - weighting gamma-encoded values
// directly biases the result toward the brighter of two neighbouring
// texels (gamma encoding is a concave curve), visible as a faint darkening/
// desaturation of soft edges under close inspection. hasSrgb (see
// llviewershadermgr.cpp) attaches the real definitions of these two.
float3 srgb_to_linear(float3 cs);
float3 linear_to_srgb(float3 cl);

struct PSInput
{
    float4 position : SV_Position;
    float2 vary_fragcoord : TEXCOORD0;
};

// Catmull-Rom (a=-0.5) basis weights, f in [0,1) = fractional distance past
// the 2nd of the 4 sampled texels.
float crw0(float f) { return f * (-0.5 + f * (1.0 - 0.5 * f)); }
float crw1(float f) { return 1.0 + f * f * (-2.5 + 1.5 * f); }
float crw2(float f) { return f * (0.5 + f * (2.0 - 1.5 * f)); }
float crw3(float f) { return f * f * (-0.5 + 0.5 * f); }

float4 main(PSInput IN) : SV_Target
{
    // S24 (2026-08-26, task #263): flip GL-origin -> D3D11-origin ONCE,
    // here, then do the entire 4-tap offset/weight computation in that
    // single already-correctly-oriented space - NOT by flipping each tap
    // independently (postDeferredGammaCorrect.hlsl's usual per-tap-flip
    // convenience is only safe for symmetric kernels like glow's; Catmull-
    // Rom's weights are NOT symmetric - w0/w3 and w1/w2 genuinely differ -
    // so flipping each tap on its own would silently reverse which weight
    // lands on which texel for the vertical pass specifically).
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);

    // glowDelta is exactly 1 source texel's UV step along the active axis
    // (see LLGPUResize::resize()) - nonzero on only one component.
    float2 step1 = glowDelta;
    float stepLen = step1.x + step1.y; // whichever component is nonzero
    bool horizontal = step1.x > step1.y;
    float axisTC = horizontal ? tc.x : tc.y;

    float texelPos = axisTC / stepLen - 0.5;
    float texelIndex = floor(texelPos);
    float f = texelPos - texelIndex;

    // S24 (2026-08-26, task #263 round 5): sample UVs built from the
    // EXPLICIT integer texel index (texelIndex+0.5, scaled back to UV),
    // not "tc +/- step1" - tc itself is an arbitrary, non-texel-aligned
    // position (it's the OUTPUT pixel's UV, mapped into a differently-
    // sized source), so offsetting it by whole-texel steps does NOT
    // reliably land each tap on an exact texel center under POINT
    // filtering - right at a texel boundary, floating-point rounding can
    // push a sample to the wrong neighbour, silently shifting all 4 taps
    // by one texel and applying the wrong Catmull-Rom weight to the wrong
    // texel. Building "base" from the known integer index first guarantees
    // every tap lands exactly on a texel center regardless of tc's own
    // fractional position - found by hand-tracing the boundary case (f=0.5)
    // after a "still jagged/crude under close inspection" report on an
    // otherwise-working build; not the AA source of that report (see
    // rawSnapshot()'s forced-FXAA comment), but a real, separate bug worth
    // fixing regardless.
    float2 base = horizontal
        ? float2((texelIndex + 0.5) * stepLen, tc.y)
        : float2(tc.x, (texelIndex + 0.5) * stepLen);

    float w0 = crw0(f);
    float w1 = crw1(f);
    float w2 = crw2(f);
    float w3 = crw3(f);

    float4 c0 = diffuseRect.Sample(diffuseRectSampler, base - step1);
    float4 c1 = diffuseRect.Sample(diffuseRectSampler, base);
    float4 c2 = diffuseRect.Sample(diffuseRectSampler, base + step1);
    float4 c3 = diffuseRect.Sample(diffuseRectSampler, base + step1 * 2.0);

    // S24 (2026-08-26, task #263 round 6): linearize -> weight -> re-encode.
    // Alpha is left alone (not a light quantity, and this pipeline's alpha
    // is a hardcoded constant anyway - see softenLightF.hlsl).
    float3 l0 = srgb_to_linear(c0.rgb);
    float3 l1 = srgb_to_linear(c1.rgb);
    float3 l2 = srgb_to_linear(c2.rgb);
    float3 l3 = srgb_to_linear(c3.rgb);

    float3 blended = w0 * l0 + w1 * l1 + w2 * l2 + w3 * l3;
    float alpha = w0 * c0.a + w1 * c1.a + w2 * c2.a + w3 * c3.a;

    return float4(linear_to_srgb(max(blended, float3(0.0, 0.0, 0.0))), alpha);
}
