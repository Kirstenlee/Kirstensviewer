/**
 * @file krlvattachment.h
 * @brief KRLV Attachments category - small, newview-facing query functions.
 * Same purpose as krlvcamera.h/krlvteleport.h/krlvchat.h: newview code
 * calls these DIRECTLY at the exact points that need to apply an
 * attach/detach/outfit-layer restriction, keeping the RLVa-specific
 * point-name/part-name matching logic inside krlv/ rather than leaking it
 * into newview code - see krlv/README.md's Attachments section.
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

#ifndef KRLV_KRLVATTACHMENT_H
#define KRLV_KRLVATTACHMENT_H

#include <string>

#include "llerror.h" // stdtypes.h anchor
#include "lluuid.h"

namespace KRlv
{
    // True if a plain "@detach=n" self-restriction is currently held BY
    // this specific object id - the ordinary "make myself undetachable"
    // form. Distinct from the point-locking queries below, which apply
    // regardless of which object occupies the point.
    bool isObjectDetachLocked(const LLUUID& objectId);

    // True if detaching FROM the named attachment point should be
    // blocked right now - covers @detach:<point>=n (see file header
    // comment in krlvattachment.cpp for why it locks both directions)
    // and @remattach[:<point>]=n. An empty pointName checks only the
    // no-point ("all points") form of those two commands.
    bool isDetachPointLocked(const std::string& pointName);

    // True if attaching TO the named attachment point should be blocked
    // right now - covers @detach:<point>=n and @addattach[:<point>]=n.
    bool isAttachPointLocked(const std::string& pointName);

    // True if ANY specific (non-wildcard) attach point currently has an
    // active lock - used as a conservative fallback when the caller
    // genuinely cannot determine which point an item will attach to
    // before it happens (rez_attachment()'s NULL-LLViewerJointAttachment*
    // path, taken when wearItemsOnAvatar() lets the server pick the
    // point) - see krlv/README.md's Attachments section for why blocking
    // outright in that case is the safe choice, not a bug.
    bool isAnySpecificAttachPointLocked();

    // True if wearing a clothing/bodypart item of this wearable-type
    // name should be blocked right now - covers @addoutfit[:<part>]=n.
    bool isOutfitPartAttachLocked(const std::string& partName);

    // True if removing a clothing/bodypart item of this wearable-type
    // name should be blocked right now - covers @remoutfit[:<part>]=n.
    bool isOutfitPartDetachLocked(const std::string& partName);

    // True only while @defaultwear=y is active - lets the "Wear"
    // inventory action bypass the point/part locks above, matching the
    // spec's own description of this command. Default (no @defaultwear
    // issued, or @defaultwear=n) is false, i.e. no bypass - the locks
    // above always apply on their own.
    bool isDefaultWearAllowed();

    // @acceptpermission / @denypermission - @denypermission always wins
    // if both are somehow active (the spec's own stated precedence).
    enum class ScriptPermissionResponse { None, AutoAccept, AutoDeny };
    ScriptPermissionResponse getScriptPermissionAutoResponse();
}

#endif // KRLV_KRLVATTACHMENT_H
