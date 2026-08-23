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

// S24 (2026-08-22, plan item A - CLOSED-FORM REWRITE): replaces the whole
// modelview_matrix + flipCol + fixHandedness + poleRotate patch history
// (task #194, 47+ "round N" comments across this file's git history) with
// Direct3D's own documented, exact TextureCube face-addressing formula
// (the same table OpenGL uses - Microsoft kept them identical for asset
// compatibility), hardcoded per face below. This pass writes pixels
// directly into face `cubeFace` of a TextureCubeArray via a full-screen
// quad + CopySubresourceRegion (see the C++ call site) - it does NOT use
// D3D11's own cubemap rasterization/array-index machinery, so nothing
// automatically guarantees the pixel written at quad position (x,y) ends
// up holding the direction D3D11's hardware sampler will later associate
// with that same (face,x,y) when this texture gets sampled for real
// (reflectionProbeF.hlsl's tapRefMap()/tapIrradianceMap()). Every previous
// uniform in this file was an empirically-discovered partial correction
// for that exact mismatch, discovered face-by-face by trial and error.
// Computing the write-side direction from the documented formula directly
// makes the two sides match by construction, for all 6 faces at once, with
// no remaining per-face degree of freedom to hand-tune.
//
// cubeFace must be D3D11's real per-face index (0=+X, 1=-X, 2=+Y, 3=-Y,
// 4=+Z, 5=-Z - LLCubeMapArray::sTargets' own convention), since this
// value also selects the destination array slice
// (probe->mCubeIndex*6+cubeFace) that hardware sampling will later read
// with its own real face addressing - it must already agree with hardware
// for indexing to be consistent at all, independent of this rewrite.
uniform int cubeFace;

// S24 (2026-08-23, LIVE ORIENTATION TUNER): per-face live-tunable knobs,
// see main()'s comment further down for the full explanation.
uniform int dbgSwap;
uniform float dbgSignA;
uniform float dbgSignB;

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

    // S24 (task #194 follow-up, 2026-08-13): IN.position.z is a constant
    // -1 for every vertex of this fixed quad (see
    // LLReflectionMapManager::initReflectionMaps()'s mVertexBuffer) - a
    // valid GL NDC-z value (GL's clip-space z range is [-1,1]), but
    // OUTSIDE D3D11's valid clip-space z range of [0,1]. Since every
    // vertex of this quad shares the identical z=-1, and this pass's
    // rasterizer state has DepthClipEnable=TRUE (default - nothing here
    // requests GL_DEPTH_CLAMP), D3D11's clipper rejects the WHOLE
    // primitive every single draw call - this pass has never actually
    // rasterized a single pixel under DX_RENDER, silently leaving
    // whatever stale/uninitialized content was already in the scratch
    // render target to be byte-copied into the destination cube array
    // face instead of real filtered data. Using 0.0 (matching
    // LLPipeline::mScreenTriangleVB's already-proven-safe convention,
    // pipeline.cpp) instead of IN.position.z here only affects
    // rasterization coverage - depth test/write are both disabled for
    // this whole pass (see the LLGLDepthTest(GL_FALSE, GL_FALSE) at this
    // function's C++ call site), so the specific z value written is
    // otherwise irrelevant.
    OUT.position = float4(IN.position.xy, 0.0, 1.0);

    // S24 (2026-08-22): Direct3D/OpenGL's documented per-face cubemap
    // addressing table (see e.g. the OpenGL wiki's "Cubemap Texture"
    // page, or D3D's identical convention), inverted to go from
    // (face, s, t) -> sample direction, s=x, t=y both in [-1,1].
    //
    // S24 (2026-08-22, follow-up): the hand-derivation above (claiming the
    // pass's negative-height viewport flip already made IN.position.y
    // equal the formula's top-origin "t" directly) was live-tested and
    // found backwards - user confirmed a clean, uniform "upside down"
    // result across every face (the controlled front/back marker test
    // landed on the correct SIDE for the first time, just inverted
    // vertically) - exactly the single, predictable sign error a formula
    // fix should produce, unlike the old per-face patch pile's jumbles.
    // Negating y here is the fix - confirmed correct for 4 of 6 faces
    // (+Y/-Y/+Z/-Z), including real, correctly-placed scene content
    // (user's own words: "I can even see the rocking chair reflected from
    // its correct location").
    //
    // S24 (2026-08-22, follow-up #2, TESTED AND REJECTED): tried the
    // opposite y sign for just +X/-X (cf==0/1) on the theory that this
    // pair needed the sign the other 4 didn't. Made it WORSE, not better -
    // user reported a new mirror-symmetric "same on both sides" duplicate
    // artifact, on top of still being upside down. Reverted to the same
    // formula as the other 4 faces (proven correct by direct derivation
    // from Direct3D/OpenGL's documented table, and empirically by the
    // other 4 faces' correct content) - this pass's own write-side math is
    // demonstrably right for every face by the same construction, so a
    // per-face sign difference here was the wrong theory.
    //
    // S24 (2026-08-22, follow-up #3): checked the capture side
    // (llviewerwindow.cpp's cubeSnapshot()) directly - look_dirs[0]/[1]
    // are exactly world (1,0,0)/(-1,0,0) as required, and DXCubeMapFaces
    // ::sUpVecs is a UNIFORM negation of GL's own up-vector table across
    // all 6 faces (not an X-specific anomaly) - it groups +X/-X with
    // +Z/-Z (both get up=(0,1,0)), but +Z/-Z are confirmed WORKING here
    // while +X/-X aren't, so the working/failing split doesn't line up
    // with anything in that table either. No smoking gun found upstream.
    // S24 (2026-08-22, follow-up #4, TESTED AND REJECTED): tried negating
    // x for just +X/-X instead (the one remaining untouched variable) -
    // also made things WORSE (user: "everythings got a little messy
    // again"), same mirror-duplicate symptom as follow-up #2. Two
    // independent single-variable sign-flip guesses for this pair have now
    // both failed - that's a real signal to stop guessing here and build
    // an actual diagnostic instead of a third blind attempt. Reverted to
    // the plain, proven-correct formula (same as all 6 faces) - the last
    // confirmed state was a LIMITED, well-understood defect for just these
    // 2 faces (right side, upside down, "zoomed out"), not the broader
    // mess either sign-flip guess produced.
    //
    // S24 (2026-08-22, follow-up #5, TESTED AND REJECTED): a TEMPORARY
    // solid-color diagnostic (since removed, see radianceGenF.hlsl)
    // confirmed face SELECTION is correct for 0/1 - two clean, correctly-
    // sized, non-overlapping, mirror-symmetric regions, no jumbling. All 4
    // possible x/y sign combinations for this pair have now been tried:
    // (+x,-y) = "right side, upside down"; (+x,+y) = "same on both sides"
    // duplicate; (-x,-y) = messy; (-x,+y) = user's annotated screenshot
    // (position.png) showed real content (a blue-glass window feature)
    // present but shifted to the WRONG on-screen position relative to
    // where matching real background content sits - not a mirror/flip
    // symptom, a genuine positional shift. That rules out the entire sign-
    // flip family for this pair.
    //
    // S24 (2026-08-22, follow-up #6, PARTIAL SUCCESS): a plain axis
    // transpose (swap which raw quad axis feeds the 2nd vs 3rd slot) for
    // just these 2 faces - user confirmed (position 2.png) this fixed the
    // POSITION (content now appears in the right place) but left it
    // rotated ("on its side"). Makes sense in hindsight: a plain swap with
    // no accompanying sign change is a REFLECTION (determinant -1, mirror
    // across the diagonal), not a rotation. The fix should be one of the
    // two swap+single-sign-flip variants, which ARE genuine 90-degree
    // rotations (determinant +1) - same family/position as the working
    // transpose, just the proper-rotation sibling of it instead of its
    // mirror-image sibling.
    //
    // S24 (2026-08-22, follow-up #7, TESTED AND REJECTED): flipping the
    // sign of just the 2nd slot (was -qx, now +qx) - user reported a
    // different, worse-looking artifact (content stretched diagonally,
    // position 3.png), not simply "still rotated" - confirms this was the
    // wrong one of the 2 rotation candidates, not a partial improvement.
    //
    // S24 (2026-08-22, follow-up #8, TESTED AND REJECTED): the other
    // rotation candidate - flipping the 3rd slot's sign instead (was -qy,
    // now +qy). User's sun-azimuth ground-truth test (position 5.png -
    // using the environment editor to place the sun at a known, unambiguous
    // horizon position, then marking matching dots on each face's copy of
    // the resulting horizon glow) showed a real vertical mismatch between
    // the two faces' content at the seam - still wrong.
    //
    // S24 (2026-08-22, follow-up #9, TESTED AND REJECTED): both slots
    // flipped relative to the plain transpose - user's position 6.png
    // sun-azimuth test still showed a real mismatch (the two horizon-glow
    // segments were in reversed relative order across the seam). All 4
    // swap-family (transpose) sign variants now rejected.
    //
    // S24 (2026-08-22, follow-up #10, TESTED AND REJECTED): the 4th
    // no-swap sign combination (below) completed the exhaustive 8-symmetry
    // sweep of the OUTPUT-side formula alone. None of the 8 fully fixed
    // +X/-X - strong evidence the remaining defect isn't a pure within-face
    // orientation bug solvable by permuting/negating qx,qy here.
    //
    // S24 (2026-08-23, follow-up #11, TESTED AND REJECTED): built a
    // TEMPORARY raw single-tap readback diagnostic (radianceGenF.hlsl,
    // bypassing prefilterEnvMap()'s GGX blur) to rule the prefilter in/out
    // - ruled OUT, the same real content showed up mis-oriented even raw
    // (SIDE 1.png/SIDE 2.png). User's read of that raw content: related to
    // correct by a 180-rotation-then-transpose, i.e. (u,v)->(-v,-u).
    // Applied as (qx,qy)->(-qy,-qx) - REJECTED (SIDE 1a.png/SIDE 2a.png):
    // user reported the content is now off by a 90-degree rotation instead
    // - still wrong, just a different wrong. Between this and follow-ups
    // #1-10, essentially the full 8-element D4 group has now been tried
    // against this same quad's (qx,qy) with no exact match ever found -
    // conclusive evidence the bug is NOT expressible as any discrete
    // reorientation of this face's own output formula. Reverted to the
    // last globally-consistent baseline (variant 4, matches faces 2-5's
    // derivation exactly) pending investigation of the CAPTURE stage
    // (cubeSnapshot()'s camera FOV/aspect/up-vector for faces 0/1
    // specifically) - the original "zoomed out" symptom (REF SIDE 1/2.png,
    // very first report on this pair) was never actually explained by any
    // orientation theory and may be the real lead: a scale/FOV mismatch
    // can't be fixed by any permutation of qx,qy, only by fixing what the
    // raw capture itself contains.
    //
    // S24 (2026-08-23, LIVE ORIENTATION TUNER): the exhaustive manual sweep
    // above cost one full rebuild + in-world round trip per hypothesis.
    // dbgSwap/dbgSignA/dbgSignB are now driven live from KVTweaks (the
    // "Cube Orient" tab's checkboxes, S24CubeOrientSwap/NegA/NegB<face> in
    // settings.xml), uploaded per-face per-mip alongside cubeFace itself
    // (see llreflectionmapmanager.cpp). The default reflection probe
    // refreshes on its own every RenderReflectionProbeUpdatePeriod seconds
    // under RenderReflectionProbeLevel=0, so toggling a checkbox live-
    // retunes a face's formula with no rebuild.
    //
    // S24 (2026-08-23, "MAGIC COMBO"): live-tuner session found a
    // combination that renders a completely clean, seamless sphere on all
    // 6 faces at once (user-confirmed, SIDE 1b.png/SIDE 2b.png) - NegA
    // flipped from its original sign on EVERY face, Swap and NegB
    // untouched everywhere. Since every face's "A" slot is fed by the same
    // raw qx (dbgSwap is off on all 6), this collapses to one clean, well-
    // motivated global transform - negate qx before it enters ANY face's
    // formula - not an arbitrary 6-way patchwork. Consistent with this
    // project's established GL-bottom-up-vs-D3D11-top-down pattern
    // (already fixed once for Y via the viewport flip; this looks like the
    // X-axis counterpart, surfacing through this formula instead). Per
    // user's explicit instruction (2026-08-23): recorded as the new
    // PROVISIONAL DEFAULT directly in settings.xml (not yet collapsed into
    // a hardcoded `qx = -qx` here) so the live tuner stays available for
    // further adjustment if new issues turn up, and only gets "baked in"
    // for real once fully happy. Caveat not yet closed out: +Y/-Y/+Z/-Z
    // were previously confirmed correct with their OLD sign using a
    // specific landmark (the rocking chair) - they need the same landmark
    // re-check under the new default before this is trusted long-term,
    // since mirrored sky/cloud content can look superficially fine.
    float qx = IN.position.x;
    float qy = IN.position.y;

    float qa = (dbgSwap != 0) ? qy : qx;
    float qb = (dbgSwap != 0) ? qx : qy;
    float freeA = dbgSignA * qa;
    float freeB = dbgSignB * qb;

    float3 dir;
    if (cubeFace == 0)      dir = float3( 1.0, freeB, freeA); // +X
    else if (cubeFace == 1) dir = float3(-1.0, freeB, freeA); // -X
    else if (cubeFace == 2) dir = float3(freeA,  1.0, freeB); // +Y
    else if (cubeFace == 3) dir = float3(freeA, -1.0, freeB); // -Y
    else if (cubeFace == 4) dir = float3(freeA, freeB,  1.0); // +Z
    else                    dir = float3(freeA, freeB, -1.0); // -Z

    OUT.vary_dir = dir;

    return OUT;
}
