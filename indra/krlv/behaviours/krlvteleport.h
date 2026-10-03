/**
 * @file krlvteleport.h
 * @brief KRLV Teleportation category - small, newview-facing query
 * functions. Unlike krlvbehaviours.h's registration functions
 * (krlv-internal, called only from krlvhandler.cpp's init()), these are
 * meant to be called FROM newview code at the exact points that need to
 * apply a distance-based teleport/sit restriction - see krlv/README.md's
 * Teleportation section for why these exist as purpose-built queries
 * rather than a generic KRlvHandler getter (keeps RLVa-specific
 * multi-source reduction logic inside krlv/ rather than leaking into
 * newview code).
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

#ifndef KRLV_KRLVTELEPORT_H
#define KRLV_KRLVTELEPORT_H

#include "llerror.h" // stdtypes.h anchor, for F32

namespace KRlv
{
    // @sittp[:max_distance] - true (and outMaxDist set) if sitting is
    // currently distance-restricted; false if @sittp isn't active at
    // all. Reduction rule and default are both taken literally from the
    // spec's own text for this command ("the viewer will restrict to the
    // minimum distance of all", default 1.5m when no source specifies a
    // distance).
    bool getSitDistanceLimit(F32& outMaxDist);

    // @tplocal[:max_distance] - same shape as getSitDistanceLimit(), for
    // same-region teleports. The spec does NOT state a multi-source
    // reduction rule or default for this command specifically - the same
    // "minimum distance of all, 0 (no exception) if any source omits a
    // distance" convention as @sittp is applied here for consistency, a
    // deliberate engineering choice documented in krlv/README.md, not an
    // inferred RLVa rule.
    bool getLocalTeleportDistanceLimit(F32& outMaxDist);
}

#endif // KRLV_KRLVTELEPORT_H
