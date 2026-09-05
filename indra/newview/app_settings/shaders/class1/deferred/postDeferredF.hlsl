/**
 * @file class1/deferred/postDeferredF.hlsl
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

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (attached below,
// gDeferredPostProgram sets isDeferred=true) - moved this file's own
// texture to t7/s7, matching the same fix as
// postDeferredGammaCorrect.hlsl's diffuseRect.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

// inv_proj/screen_res are also declared by deferredUtil.hlsl, grouped
// together there under one guard. inv_proj itself is never referenced in
// this file, but deferredUtil.hlsl's own getPosition() (attached
// regardless of whether this file calls it - its body still needs to
// type-check) uses inv_proj internally, so both names must be declared
// together here even though only screen_res is used below - matching the
// exact same declared set as deferredUtil.hlsl's guarded block (same
// "guard grouping" lesson as blurLightF.hlsl's fix earlier this session -
// deleting inv_proj alone would leave it undeclared once this block's
// guard trips deferredUtil.hlsl's later one to skip).
#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif
uniform float max_cof;
uniform float res_scale;

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

void dofSample(inout float4 diff, inout float w, float min_sc, float2 tc)
{
    // SampleLevel (explicit LOD), not Sample (implicit gradient/derivative
    // computation) - this function is called from inside a loop whose
    // iteration count varies per-pixel (its, computed at runtime), and a
    // gradient instruction inside such a loop forces D3DCompile to attempt
    // an implicit unroll regardless of any [unroll]/[loop] attribute
    // (X3570 warning cascading into the same X3511 "unable to unroll"
    // error as the loop-count issue above) - GLSL's plain texture() call
    // has no equivalent restriction, so there's nothing to match in the
    // original source here, this is a standard HLSL-only fix. LOD 0 is
    // correct since this is a full-resolution post-process buffer with no
    // mip chain.
    float4 s = diffuseRect.SampleLevel(diffuseRectSampler, tc, 0);

    float sc = abs(s.a * 2.0 - 1.0) * max_cof;

    if (sc > min_sc)
    {
        float wg = 0.25;
        wg += s.r + s.g + s.b;
        diff += wg * s;
        w += wg;
    }
}

void dofSampleNear(inout float4 diff, inout float w, float min_sc, float2 tc)
{
    // SampleLevel, not Sample - same reasoning as dofSample() above.
    float4 s = diffuseRect.SampleLevel(diffuseRectSampler, tc, 0);

    float wg = 0.25;
    wg += s.r + s.g + s.b;
    diff += wg * s;
    w += wg;
}

float3 clampHDRRange(float3 color);

float4 main(PSInput IN) : SV_Target
{
    // S24 (2026-08-11, quick-win origin sweep): GL-vs-D3D11 texture-origin
    // flip - tc is used only for diffuseRect reads in this file (main's own
    // Sample() below, plus dofSample()/dofSampleNear()'s SampleLevel()
    // calls, which just take whatever tc is passed to them), so it's safe
    // to flip once here. Same bug class as task #158/#185.
    float2 tc = float2(IN.vary_fragcoord.x, 1.0 - IN.vary_fragcoord.y);

    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc);

    // S24 (2026-08-31, DoF feathering fix v2): kept aside, untouched, so the
    // sc<=0.5 dead-zone below can stay bit-for-bit identical to it (see the
    // gate's own comment for why that guarantee matters).
    float4 sharp = diff;

    {
        float w = 1.0;
        float sc = (diff.a * 2.0 - 1.0) * max_cof;
        static const float PI = 3.14159265358979323846264;
        float feather = 0.0;

        // S24 (2026-08-31, DoF feathering fix v2): the 0.5px GATE ITSELF is
        // deliberately UNCHANGED from stock - a prior attempt to lower it
        // (see project_dxrender_dof_alpha_mitigation_2026_08_31.md memory)
        // let dofCombineF.hlsl's blend pull in the half-resolution blur
        // buffer for almost any nonzero CoC, and since that buffer is a
        // genuinely lower-resolution reconstruction of the scene, blending
        // even a little of it in everywhere read as a resolution-mismatch
        // shimmer across the whole frame - reverted. The ACTUAL "hard
        // threshold pop" the gate itself was blamed for turned out to be a
        // different, narrower problem: the moment the FIRST sample is ever
        // taken (sc just above 0.5), the normalization weight `w` jumps
        // from exactly 1.0 (100% center/sharp) to roughly 1+wg in one step
        // - wg (0.25 plus a sample's own RGB) is not small, so that first
        // sample is a substantial blend, not a gentle start, regardless of
        // how close sc is to the 0.5 boundary. `feather` ramps that blend
        // in smoothly over sc in [0.5, 1.0] instead - sc<=0.5 still takes
        // the gate exactly as before (feather stays 0.0, diff stays
        // `sharp`, byte-identical to stock), only the transition just above
        // the boundary is smoothed, not the boundary's existence.
        if (sc > 0.5)
        {
            feather = smoothstep(0.5, 1.0, sc);
            while (sc > 0.5)
            {
                int its = int(max(1.0, (sc * 3.7)));
                // its is a runtime value (derived from a texture sample),
                // never a compile-time constant - [unroll] can never
                // resolve this and forces D3DCompile to give up
                // (X3511, tried up to 1024 iterations). The original GLSL
                // has no unroll-equivalent directive here at all - a plain
                // for loop with a dynamic bound just compiles to a real
                // loop. Removed; this is a genuine dynamic loop, not a
                // fixed-count one like the light loops unrolled elsewhere
                // this session.
                for (int i = 0; i < its; ++i)
                {
                    float ang = sc + i * 2 * PI / its;
                    float samp_x = sc * sin(ang);
                    float samp_y = sc * cos(ang);
                    dofSampleNear(diff, w, sc, tc + (float2(samp_x, samp_y) / screen_res));
                }
                sc -= 1.0;
            }
        }
        else if (sc < -0.5)
        {
            feather = smoothstep(0.5, 1.0, -sc);
            sc = abs(sc);
            while (sc > 0.5)
            {
                int its = int(max(1.0, (sc * 3.7)));
                // its is a runtime value (derived from a texture sample),
                // never a compile-time constant - [unroll] can never
                // resolve this and forces D3DCompile to give up
                // (X3511, tried up to 1024 iterations). The original GLSL
                // has no unroll-equivalent directive here at all - a plain
                // for loop with a dynamic bound just compiles to a real
                // loop. Removed; this is a genuine dynamic loop, not a
                // fixed-count one like the light loops unrolled elsewhere
                // this session.
                for (int i = 0; i < its; ++i)
                {
                    float ang = sc + i * 2 * PI / its;
                    float samp_x = sc * sin(ang);
                    float samp_y = sc * cos(ang);
                    dofSample(diff, w, sc, tc + (float2(samp_x, samp_y) / screen_res));
                }
                sc -= 1.0;
            }
        }

        diff /= w;
        diff = lerp(sharp, diff, feather);
    }

    diff.rgb = clampHDRRange(diff.rgb);
    return diff;
}
