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

// Separable 4-tap Catmull-Rom (a=-0.5) bicubic resize: this shader is bound
// TWICE per resize (horizontal then vertical) with a different glowDelta
// each time, same reuse pattern as gGlowProgram's separable blur.
// glowDelta is glow's own uniform, reused here rather than adding a new name.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

uniform float2 glowDelta;

// Blend the 4 taps in linear light, not raw gamma-encoded values (this
// shader's input is the post-gamma-correct composited target) - weighting
// gamma-encoded values directly biases toward the brighter neighbour texel.
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
    // Flip GL-origin -> D3D11-origin ONCE, then do the whole 4-tap
    // computation in that space - NOT per-tap. Catmull-Rom's weights are
    // NOT symmetric (w0/w3 and w1/w2 differ), so flipping each tap
    // independently would reverse which weight lands on which texel.
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

    // Sample UVs are built from the EXPLICIT integer texel index
    // (texelIndex+0.5, scaled back to UV), not "tc +/- step1": tc is an
    // arbitrary non-texel-aligned position, so offsetting it by whole-texel
    // steps risks floating-point rounding pushing a sample to the wrong
    // neighbour right at a texel boundary, shifting all 4 taps by one texel.
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

    // Linearize -> weight -> re-encode. Alpha is left alone (not a light
    // quantity, and this pipeline's alpha is a hardcoded constant anyway).
    float3 l0 = srgb_to_linear(c0.rgb);
    float3 l1 = srgb_to_linear(c1.rgb);
    float3 l2 = srgb_to_linear(c2.rgb);
    float3 l3 = srgb_to_linear(c3.rgb);

    float3 blended = w0 * l0 + w1 * l1 + w2 * l2 + w3 * l3;
    float alpha = w0 * c0.a + w1 * c1.a + w2 * c2.a + w3 * c3.a;

    return float4(linear_to_srgb(max(blended, float3(0.0, 0.0, 0.0))), alpha);
}
