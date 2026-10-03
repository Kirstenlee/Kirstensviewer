/**
 * @file krlvtouch.h
 * @brief KRLV "Touch" category - small, newview-facing query functions.
 * Same purpose as krlvcamera.h/krlvteleport.h/krlvattachment.h/
 * krlvname.h: newview code calls these DIRECTLY at the exact points
 * that already decide whether a touch/grab should proceed, keeping the
 * RLVa-specific scoping logic inside krlv/ rather than leaking it into
 * newview code - see krlv/README.md's Touch section.
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

#ifndef KRLV_KRLVTOUCH_H
#define KRLV_KRLVTOUCH_H

#include "llerror.h" // stdtypes.h anchor
#include "lluuid.h"

namespace KRlv
{
    // @touchall/@touchworld[:<UUID>]/@touchthis:<UUID>/@touchme/
    // @touchattach/@touchattachself/@touchattachother[:<UUID>]/
    // @touchhud[:<UUID>]/@interact - true if touching/grabbing this
    // object should be blocked right now. `isSelfAttachment`/
    // `attachedAvatarId` are only meaningful when `isAttachment` is
    // true; `attachedAvatarId` is the owning avatar's id (null if it
    // couldn't be resolved, e.g. a still-rezzing attachment).
    bool isTouchBlocked(const LLUUID& objectId, bool isAttachment, bool isHud,
                         bool isSelfAttachment, const LLUUID& attachedAvatarId);

    // @fartouch[:max_distance]/@touchfar (same key, a stated synonym) -
    // true (and outMaxDist set, minimum of every active source's
    // distance, default 1.5m per the spec's own text) if touch is
    // currently distance-restricted. Never applies to HUDs (the spec's
    // "has to press against the object" framing is inherently about
    // in-world/attachment proximity).
    bool getTouchDistanceLimit(F32& outMaxDist);

    // @interact - also checked by Inventory's @edit/@rez gates and
    // Sitting's @sit gate (the spec explicitly states @interact also
    // blocks editing, rezzing and sitting), in ADDITION to those
    // categories' own restriction keys.
    bool isInteractBlocked();
}

#endif // KRLV_KRLVTOUCH_H
