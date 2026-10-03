/**
 * @file krlvsharedfolders.h
 * @brief KRLV "Shared Folders" category - small, newview-facing pieces:
 * the well-known "#RLV" folder name, and the two query functions the
 * @unsharedwear/@unsharedunwear/@sharedwear/@sharedunwear gates need at
 * their (already-KRLV-annotated) chokepoints in llappearancemgr.cpp -
 * see krlv/README.md's Shared Folders section. Everything else this
 * category needs stays krlv-internal (krlvsharedfolders.cpp), reached
 * from newview only through the data-fetching hook family declared in
 * krlvhandler.h (KRlvGetSharedRootRequest and friends).
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

#ifndef KRLV_KRLVSHAREDFOLDERS_H
#define KRLV_KRLVSHAREDFOLDERS_H

#include <string>
#include <vector>

#include "llerror.h" // stdtypes.h anchor
#include "lluuid.h"

// KRLV: the top-level inventory folder RLVa's spec names literally -
// "the folder named #RLV" - mirrored on this viewer's own S24 AO
// "#Kirstens" convention (ROOT_KV_FOLDER, llinventoryfunctions.h) via
// the same find-or-create pattern (see KRlvGetSharedRootRequest's
// implementation in llappviewer.cpp).
#define KRLV_SHARED_ROOT_FOLDER "#RLV"

namespace KRlv
{
    // @unsharedwear/@sharedwear - true if wearing `itemId` should be
    // blocked right now (checks whether it's currently under "#RLV" via
    // KRlvIsSharedItemRequest and combines with whichever of the two
    // restrictions is active - they use opposite conditions).
    bool isWearBlockedByShareRule(const LLUUID& itemId);

    // @unsharedunwear/@sharedunwear - same idea, for removing `itemId`.
    bool isUnwearBlockedByShareRule(const LLUUID& itemId);

    // @detach:<folder_name>=force's folder-content meaning - the
    // fallback branch of the shared "detach" force hook (see
    // krlvattachment.cpp's file header and krlv/README.md for the name
    // collision this resolves) when `folderName` doesn't match a real
    // attachment point or wearable-type name. Returns the item ids
    // directly inside a folder of that name under "#RLV", empty if none
    // found.
    std::vector<LLUUID> resolveDetachFolderContents(const std::string& folderName);
}

#endif // KRLV_KRLVSHAREDFOLDERS_H
