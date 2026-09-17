/**
 * @file class1/interface/uiHueShiftF.hlsl
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

// S24: variant of uiF.hlsl (same vertex stage, uiV.hlsl, reused unmodified) that additionally
// rotates the hue of the SAMPLED TEXEL's own color, not just the CPU-side vertex tint. This is
// the only way to genuinely recolor a texture-based UI asset (floater window chrome, tabs) whose
// own baked pixels carry real color - a CPU-side tint can only ever MULTIPLY onto what's already
// there (LLUIColorTable-based hue-shift, see newview/lluihueshift.cpp), which is a no-op on
// grayscale UI and can never brighten/re-hue a genuinely dark-but-colored texture region either.
// Deliberately a SEPARATE shader/program (gUIHueShiftProgram, llrender/llhlslshader.cpp) rather
// than a uniform added to the plain gUIProgram/uiF.hlsl - gUIProgram is bound for the ENTIRE rest
// of UI rendering (buttons, scrollbars, text, icons), and those already recolor correctly via the
// existing CPU/table mechanism; adding a global uniform there would double-apply/recolor things
// that don't need it. This shader is bound ONLY at the specific call sites that need it (currently
// LLFloater::draw()'s background image draw, llui/llfloater.cpp), then gUIProgram is rebound
// immediately after, so nothing else in the frame is affected.

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

#include "varying/uiVarying.hlsli"

// S24: uiV.hlsl's VS output assigns SV_Position to register 0, shifting
// TEXCOORD0/COLOR0 up by one. PSInput must declare SV_Position first too,
// or the VS/PS interpolant registers mismatch.
struct PSInput
{
    float4 position : SV_Position;
    UIVarying varying;
};

// S24: hue shift amount in turns (0..1, wraps), 0 = fully off. Kept as a real uniform (not a
// #define/permutation) so it can change every frame from a live KVTweaks slider without
// recompiling - this shader only gets bound while a shift is actually active (see the C++ call
// site), but the 0-check here is kept anyway as a cheap, correct no-op guard.
uniform float uiHueShiftTurns;

// S24: global accessibility/aesthetic toggles, independent of the per-category hue shift above -
// LLUI::bindUIEffectsShader() (llui/llui.cpp) binds this shader whenever ANY of these is active,
// even if hue shift itself is 0 for that call site's category. Plain floats throughout - not
// worth real bool uniform types for a handful of flags read once per draw call.
uniform float uiContrast;  // 1.0 = neutral, <1.0 = lower contrast, >1.0 = higher contrast
uniform float uiGrayscale; // 0.0/1.0
uniform float uiShine;     // 0.0 = off, 1.0 = strongest diagonal sheen highlight

// S24: standard scalar RGB<->HSL conversion, written plainly (no SIMD/vectorized cleverness) -
// see llmath/v3color.cpp's own hueToRgb() fix earlier this session for why: an over-engineered
// AVX implementation of this exact same math produced genuinely out-of-range results for some
// hue inputs. This is deliberately the simple, easy-to-verify-by-inspection version.
float hueToRgbChannel(float p, float q, float t)
{
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

float3 rgbToHsl(float3 c)
{
    float maxc = max(c.r, max(c.g, c.b));
    float minc = min(c.r, min(c.g, c.b));
    float l = (maxc + minc) * 0.5;
    float h = 0.0;
    float s = 0.0;
    float delta = maxc - minc;

    if (delta > 1e-6)
    {
        s = (l < 0.5) ? delta / (maxc + minc) : delta / (2.0 - maxc - minc);

        if (maxc == c.r)
            h = (c.g - c.b) / delta + (c.g < c.b ? 6.0 : 0.0);
        else if (maxc == c.g)
            h = (c.b - c.r) / delta + 2.0;
        else
            h = (c.r - c.g) / delta + 4.0;

        h /= 6.0;
    }

    return float3(h, s, l);
}

float3 hslToRgb(float3 hsl)
{
    float h = hsl.x;
    float s = hsl.y;
    float l = hsl.z;

    if (s <= 1e-6)
    {
        // S24: pure gray - no hue to show regardless of rotation, matches the accepted
        // behavior of the CPU-side table shift for the same case (zero saturation is
        // mathematically hue-invariant, not a bug).
        return float3(l, l, l);
    }

    float q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
    float p = 2.0 * l - q;

    return float3(
        hueToRgbChannel(p, q, h + 1.0 / 3.0),
        hueToRgbChannel(p, q, h),
        hueToRgbChannel(p, q, h - 1.0 / 3.0));
}

float4 main(PSInput IN) : SV_Target
{
    float4 base = IN.varying.vertex_color * diffuseMap.Sample(diffuseMapSampler, IN.varying.vary_texcoord0.xy);
    float3 rgb = base.rgb;

    if (uiHueShiftTurns != 0.0)
    {
        float3 hsl = rgbToHsl(rgb);
        hsl.x = frac(hsl.x + uiHueShiftTurns);
        rgb = hslToRgb(hsl);
    }

    if (uiContrast != 1.0)
    {
        rgb = saturate((rgb - 0.5) * uiContrast + 0.5);
    }

    if (uiShine > 0.0)
    {
        // S24: fixed-position diagonal glass-sheen band (classic UI bevel/gloss look) - a single
        // static UV-space gradient, not a true animated specular sweep, deliberately kept this
        // cheap since the ask was for a cheap effect lever. uiShine controls the highlight's
        // brightness, not its width, so it stays a soft band rather than a hard stripe at any
        // strength.
        float diag = IN.varying.vary_texcoord0.x + (1.0 - IN.varying.vary_texcoord0.y); // 0..2
        float band = saturate(1.0 - abs(diag - 1.0) / 0.35);
        band = band * band; // soften falloff
        rgb = saturate(rgb + band * uiShine);
    }

    if (uiGrayscale > 0.5)
    {
        // S24: standard luminance-weighted grayscale (not HSL's L channel, which isn't
        // perceptually weighted). Applied last so it's a predictable final override if somehow
        // combined with the effects above.
        float gray = dot(rgb, float3(0.299, 0.587, 0.114));
        rgb = float3(gray, gray, gray);
    }

    return float4(rgb, base.a);
}
