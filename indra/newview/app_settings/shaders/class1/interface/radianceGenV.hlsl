/**
 * @file class1/interface/radianceGenV.hlsl
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

// S24 (2026-08-31, DXCubeMap rewrite plan, Step 3): PROVEN CLOSED FORM.
// The task #194 "round 1-11" + "LIVE ORIENTATION TUNER" saga (see SVN
// history for this file pre-r3724 for the full multi-week investigation)
// ended with a live-tuned combination (dbgSwap/dbgSignA/dbgSignB, driven
// from KVTweaks' "Cube Orient" tab) that rendered a clean, seamless sphere
// on all 6 faces. It was left as a live-tunable "magic combo" rather than
// hardcoded, with an explicit open caveat that it had only been validated
// against smooth sphere content, never hard-edged geometry.
//
// This rewrite independently re-derived the write-side direction formula
// from first principles - Direct3D/OpenGL's own documented TextureCube
// per-face addressing table (sc/tc/ma per face, s=(sc/ma+1)/2 etc. - see
// the OpenGL spec's "Cubemap Texture Selection" table, which D3D
// deliberately matches) - then solved for the sign/swap values that would
// reproduce that exact table, using the ACTUAL live-tuned values from
// app_settings/settings.xml's S24CubeOrientNegA/NegB/Swap0-5 declarations
// (NOT the LLCachedControl fallback defaults hardcoded in
// llreflectionmapmanager.cpp's s24GetCubeOrientDebug() - those two
// disagree on almost every face, a real trap this rewrite's first attempt
// fell into and had to correct after a live sphere-test regression).
// Result: the settings.xml "magic combo" matches the textbook formula
// EXACTLY on all 6 faces, with one clean, explainable global term (a full
// 180-degree UV rotation - both x and y of this pass's quad-space position
// are inverted relative to the table's direct (sc,tc)=(ma*x,ma*y) mapping,
// plausibly from this pass's own screen-space quad/viewport convention,
// not a per-face anomaly). Zero remaining unexplained degrees of freedom.
// The "magic combo" was not a lucky empirical guess - it IS the correct
// closed-form answer, confirmed by independent derivation, not just
// sphere content.
//
// Conclusion this proof implies: this file's direction math is not the
// source of the box-probe/shiny-cube "inside-out" reflection symptom that
// motivated the wider DXCubeMap rewrite - that bug lives elsewhere (leading
// suspect: capture-stage front/back-face culling/winding, not orientation
// math). See the DXCubeMap rewrite plan for what's next.
//
// This pass writes pixels directly into face `cubeFace` of a TextureCubeArray
// via a full-screen quad + CopySubresourceRegion (see the C++ call site) -
// it does NOT use D3D11's own cubemap rasterization/array-index machinery,
// so the formula below must independently match what D3D11's hardware
// sampler will later associate with (face,x,y) when this texture gets
// sampled for real (reflectionProbeF.hlsl's tapRefMap()/tapIrradianceMap()).
//
// cubeFace must be D3D11's real per-face index (0=+X, 1=-X, 2=+Y, 3=-Y,
// 4=+Z, 5=-Z), since this value also selects the destination array slice
// (probe->mCubeIndex*6+cubeFace) that hardware sampling will later read
// with its own real face addressing.
uniform int cubeFace;

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_dir : TEXCOORD0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    // S24 (task #194 follow-up, 2026-08-13): IN.position.z is a constant -1
    // for every vertex of this fixed quad (see
    // LLReflectionMapManager::initReflectionMaps()'s mVertexBuffer) - valid
    // under GL's [-1,1] clip-z range but outside D3D11's [0,1] range, which
    // silently clips the whole primitive away every draw call under
    // DX_RENDER (DepthClipEnable=TRUE by default). 0.0 matches
    // LLPipeline::mScreenTriangleVB's already-proven-safe convention; depth
    // test/write are both disabled for this whole pass so the specific z
    // value written is otherwise irrelevant.
    OUT.position = float4(IN.position.xy, 0.0, 1.0);

    // S24 (2026-08-31): proven closed-form per-face direction (see header
    // comment above for the derivation). x/y are this quad's [-1,1]
    // position; the sign pattern below is Direct3D/OpenGL's documented
    // TextureCube table with one global 180-degree UV rotation (both x and
    // y run opposite the table's direct (sc,tc)=(ma*x,ma*y) mapping).
    //
    // S24 (2026-09-02, empty-higher-mip investigation, REVERTED same day):
    // an attempt to move this Y-flip out of the C++ call sites' negative-
    // height D3D11_VIEWPORT and into a `y` negation here (claimed
    // mathematically equivalent) made zero difference to the empty-mip
    // symptom it was meant to investigate, and broke hero-probe mirror
    // orientation (confirmed live, "Mirror Fault.PNG" - upside-down
    // reflection). The equivalence proof had a real flaw somewhere that
    // re-deriving it twice didn't surface (a live asymmetry was found -
    // DXRenderTarget::bindTarget() always sets a normal, non-flipped
    // viewport, and hero-probe's first mip never got an explicit
    // override unlike every other call site - but chasing that produced
    // a contradiction with the equivalence proof rather than resolving
    // it). Reverted to the proven-correct pairing: unmodified y here,
    // negative-height viewport at the C++ call sites.
    float x = IN.position.x;
    float y = IN.position.y;

    float3 dir;
    if (cubeFace == 0)      dir = float3( 1.0,    y,    x); // +X
    else if (cubeFace == 1) dir = float3(-1.0,    y,   -x); // -X
    else if (cubeFace == 2) dir = float3(  -x,  1.0,   -y); // +Y
    else if (cubeFace == 3) dir = float3(  -x, -1.0,    y); // -Y
    else if (cubeFace == 4) dir = float3(  -x,    y,  1.0); // +Z
    else                    dir = float3(   x,    y, -1.0); // -Z

    OUT.vary_dir = dir;

    return OUT;
}
