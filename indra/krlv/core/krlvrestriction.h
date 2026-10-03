/**
 * @file krlvrestriction.h
 * @brief KRLV active-restriction storage shape.
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

#ifndef KRLV_KRLVRESTRICTION_H
#define KRLV_KRLVRESTRICTION_H

#include <string>
#include <unordered_map>
#include <vector>

// KRLV: LL headers assume stdtypes.h (S32/U8/F64/...) is already visible -
// llerror.h is the same "always include first" anchor dxrender's own
// headers use for exactly this reason.
#include "llerror.h"
#include "lluuid.h"

// KRLV: a real RLV restriction is reference-counted per SOURCE object, not
// a single bool - two different objects can each impose "@sendchat=n"
// independently, and it must stay active until the LAST one releases it.
// One entry = one object currently holding one behaviour active.
struct KRlvRestrictionEntry
{
    LLUUID sourceObjectId;
    std::string option;   // the ":option" part at the time this was added, if any
};

// KRLV: the whole active-restriction table. Keyed by behaviour name
// (lowercase, matches KRlvCommand::behaviour) rather than an enum for
// stage 1 - a behaviour whose handler hasn't been written yet can still be
// recorded here (e.g. from @clear bookkeeping) without every behaviour
// needing an enumerator up front. May move to an enum key later once the
// full behaviour set is implemented, purely as a perf/typo-safety
// refinement - the storage SHAPE (reference-counted, per-source) is the
// part that has to be right from day one.
using KRlvRestrictionTable = std::unordered_map<std::string, std::vector<KRlvRestrictionEntry>>;

#endif // KRLV_KRLVRESTRICTION_H
