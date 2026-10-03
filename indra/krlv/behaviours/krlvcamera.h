/**
 * @file krlvcamera.h
 * @brief KRLV Camera category - small, newview-facing query functions.
 * Unlike krlvbehaviours.h's registration functions (krlv-internal, called
 * only from krlvhandler.cpp's init()), these are meant to be called FROM
 * newview code (llviewercamera.cpp, llagentcamera.cpp) at the exact
 * points that need to apply a camera restriction - see krlv/README.md's
 * Camera section for why these exist as purpose-built queries rather
 * than a generic KRlvHandler getter (keeps RLVa-specific math, like the
 * zoom-multiplier-to-FOV conversion, inside krlv/ rather than leaking
 * into newview code - a touchpoint should see at most one clean query).
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
 *
 * KRLV is a clean-room implementation, written entirely from the public
 * RLVa API specification (the command names, syntax and behaviour
 * described at
 * https://wiki.secondlife.com/wiki/LSL_Protocol/RestrainedLoveAPI), with
 * respect and thanks to Marine Kelley, who created the original
 * RestrainedLove API and viewer, and Kitty Barnett, who created RLVa
 * (Restrained Love Viewer - Advanced). No code from their viewers, or from
 * any RLV/RLVa source tree, was read, copied, or referenced in writing this
 * module - only the published API document.
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

#ifndef KRLV_KRLVCAMERA_H
#define KRLV_KRLVCAMERA_H

#include "llerror.h" // stdtypes.h anchor, for F32
#include "lluuid.h"

namespace KRlv
{
    // @camzoommax/@camzoommin (a zoom MULTIPLIER, 1.0 = default) already
    // reduced across sources per the spec's own per-command rule, then
    // converted to a field-of-view bound in radians using the same
    // DEFAULT_FIELD_OF_VIEW baseline the viewer's own Ctrl-0/Ctrl-8 zoom
    // handler (LLZoomer) uses. Returns false (bounds untouched) if
    // neither is active. Apply as an INDEPENDENT llclamp() call from
    // getFovBoundsDirect() below - sequential clamping naturally yields
    // the intersection of both if both families are active at once.
    bool getFovBoundsFromZoomMultiplier(F32& outFloorFov, F32& outCeilFov);

    // @setcam_fovmin/@setcam_fovmax - same shape, already in radians, no
    // conversion needed.
    bool getFovBoundsDirect(F32& outFloorFov, F32& outCeilFov);

    // @camdistmax/@setcam_avdistmax (ceiling - exact synonyms, share one
    // internal restriction key) and @camdistmin/@setcam_avdistmin (floor -
    // same). Does NOT implement the spec's "distance=0 forces Mouselook"/
    // "distance>0 forces OUT of Mouselook" side effects - just the
    // distance clamp itself; see krlv/README.md.
    bool getCamDistanceBounds(F32& outFloorDist, F32& outCeilDist);

    // @camdrawmin/@camdrawmax/@camdrawalphamin/@camdrawalphamax/
    // @camdrawcolor - a distance-based fog-blind effect. Returns false
    // (all out-params untouched) if neither @camdrawmin nor @camdrawmax
    // has an active source - the pair of distances is the one thing this
    // query requires to be genuinely defined before the effect activates
    // at all; the alpha/color out-params get sensible defaults (0.0,
    // 1.0, black) when their own commands aren't active, matching the
    // spec's own stated default color. min/max distance and min/max
    // alpha each reduce across sources toward the MOST RESTRICTIVE value
    // (closest fog start, soonest full opacity, most opaque) - the same
    // convention getCamDistanceBounds()/getFovBoundsFromZoomMultiplier()
    // above already use, a documented engineering choice (the source
    // dataset's own description for this row was empty) not a confirmed
    // spec reading. @camdrawcolor is the one exception - its own spec
    // text explicitly states active sources' colors are averaged
    // ("mix of all"), not reduced to an extreme.
    bool getCamDrawParams(F32& outMinDist, F32& outMaxDist,
                           F32& outMinAlpha, F32& outMaxAlpha,
                           F32& outColorR, F32& outColorG, F32& outColorB);

    // @camavdist - turns avatars farther than <distance> from the camera
    // into the existing "too complex" jellydoll ghost (translucent black
    // silhouette + rim glow, LLVOAvatar::isTooComplex()/
    // getOverallAppearance()) rather than a new render mode - the jellydoll
    // visual already matches the spec's own "visually muted... pitch black"
    // wording. Returns false (outDistance untouched) if no source is
    // active. Multiple sources reduce to the MOST RESTRICTIVE (smallest)
    // distance - undocumented in the spec's own row for this command, same
    // convention as getCamDistanceBounds()/getCamDrawParams() above.
    bool getCamAvDistLimit(F32& outDistance);

    // @camtextures/@setcam_textures (exact synonyms, share one internal
    // restriction key) - true while any source is currently blanking
    // in-world textures. outSubstituteTextureId is the first active
    // source's :texture_uuid option resolved to a real LLUUID
    // (LLUUID::null if that source gave none, or if none of the active
    // sources specified one) - the spec states no multi-source reduction
    // rule for this command; "first active source with an option wins" is
    // a documented engineering choice, not an inferred RLVa rule. Checked
    // ONCE per frame (DXPipeline::renderGeomDeferred(), gates
    // DXStateCache::sTagAttachmentStencilActive), not per-batch - see
    // krlv/README.md's Camera section.
    bool isCamTexturesActive(LLUUID& outSubstituteTextureId);
}

#endif // KRLV_KRLVCAMERA_H
