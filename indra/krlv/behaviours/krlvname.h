/**
 * @file krlvname.h
 * @brief KRLV "Name Tags and Hovertext" category - small, newview-facing
 * query functions. Same purpose as krlvcamera.h/krlvteleport.h/
 * krlvchat.h/krlvattachment.h: newview code calls these DIRECTLY at the
 * exact points that already decide whether to show a name or hovertext
 * string, keeping the RLVa-specific exception-UUID matching inside
 * krlv/ rather than leaking it into newview code - see
 * krlv/README.md's Name Tags and Hovertext section.
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

#ifndef KRLV_KRLVNAME_H
#define KRLV_KRLVNAME_H

#include "llerror.h" // stdtypes.h anchor
#include "lluuid.h"

namespace KRlv
{
    // @shownames[:except_uuid] / @shownames_sec (shares the same key) /
    // @shownametags - true if this avatar's floating nametag (identity
    // line AND chat bubble, both live on the same LLHUDNameTag - see
    // krlv/README.md for why the coarser gate was chosen) should be
    // hidden right now. `avatarId` is checked against @shownames'
    // exception UUID if one was given - @shownametags has no exception
    // syntax of its own.
    bool isNameTagHidden(const LLUUID& avatarId);

    // @shownames[:except_uuid] / @shownames_sec only - NOT @shownametags,
    // which the spec explicitly states does not censor chat. True if
    // text naming this avatar (chat sender, tooltip) should be replaced
    // with a dummy string.
    bool isNameCensored(const LLUUID& avatarId);

    // @shownearby - true if avatar names should be hidden in the minimap
    // hover tooltip. No exception syntax. The People window's Nearby-tab
    // list is NOT covered - see krlv/README.md for why.
    bool isNearbyNameHidden();

    // @showhovertextall / @showhovertext:<UUID> / @showhovertexthud /
    // @showhovertextworld - true if hovertext (llSetText() floating 2D
    // text) belonging to `sourceObjectId` should be hidden right now.
    // `sourceObjectId` may be null (e.g. beacon text with no owning
    // object) - only the point-blind global/HUD/world restrictions still
    // apply in that case. `isHudAttachment` is whatever the caller
    // already knows at its own chokepoint.
    bool isHovertextHidden(const LLUUID& sourceObjectId, bool isHudAttachment);
}

#endif // KRLV_KRLVNAME_H
