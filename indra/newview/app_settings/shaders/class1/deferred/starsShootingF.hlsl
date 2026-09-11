/**
 * @file class1/deferred/starsShootingF.hlsl
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

#include "varying/starsShootingVarying.hlsli"

// S24 (2026-09-05): originally reused starsF.hlsl's point-star daylight
// curve (custom_alpha, driven by the active preset's Star Brightness
// setting) - user feedback: a bright moon could push that value low enough
// to "eradicate" shooting stars on nights when the sun was still well
// below the horizon, since Star Brightness is an artist-authored day-cycle
// curve, not a measure of whether the sun is actually up. Switched to a
// real geometric sun-elevation gate instead - see
// dxdrawpoolwlsky.cpp's renderShootingStarsDeferred() for where
// sun_elevation is computed/bound (LLSettingsSky::getSunDirection().mV[2],
// the normalized sun direction's vertical component - 0 at the horizon,
// same convention LLSettingsSky::getIsSunUp() itself uses).
uniform float sun_elevation;

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input.
struct PSInput
{
    float4 position : SV_Position;
    StarsShootingVarying varying;
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // texcoord0.x: 0 at tail -> 1 at head. texcoord0.y: 0..1 across width.
    float u = IN.varying.vary_texcoord0.x;
    float v = IN.varying.vary_texcoord0.y;

    // bright, tight head; long soft fading tail - not a linear ramp, the
    // head should read as a distinct point of light with a trail behind it
    float head_glow = pow(saturate(u), 3.0);
    float trail = saturate(u) * 0.6;
    float streak = saturate(head_glow + trail * u);

    // soft edge across the width so it doesn't look like a hard-edged bar
    float width_falloff = 1.0 - saturate(abs(v - 0.5) * 2.0);
    width_falloff = width_falloff * width_falloff;

    float envelope = IN.varying.vertex_color.a;
    // Fully visible once the sun is a little below the horizon (~-3deg,
    // sin(-3deg)=-0.05), fully hidden once it's comfortably up (~8.6deg,
    // sin(8.6deg)=0.15) - a civil-twilight-width fade, independent of moon
    // brightness or the active preset's own Star Brightness curve.
    float daylight_factor = 1.0f - smoothstep(-0.05f, 0.15f, sun_elevation);

    float3 streak_color = lerp(float3(0.65, 0.75, 1.0), float3(1.0, 1.0, 1.0), head_glow);

    float alpha = streak * width_falloff * envelope * daylight_factor * 6.0;

    OUT.data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    OUT.data2 = float4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

    float4 col = float4(streak_color * alpha, alpha);

#if defined(HAS_EMISSIVE)
    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data3 = col;
#else
    OUT.data0 = col;
#endif

    return OUT;
}
