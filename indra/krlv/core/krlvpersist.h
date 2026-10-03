/**
 * @file krlvpersist.h
 * @brief KRLV restriction-state persistence - saves/loads the active
 * restriction table across sessions and crashes, with a tamper-evidence
 * signature. See krlv/README.md's "Persistence" section for the full
 * design note, including what this deliberately does NOT (and cannot)
 * protect against.
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

#ifndef KRLV_KRLVPERSIST_H
#define KRLV_KRLVPERSIST_H

#include <string>
#include <unordered_set>

#include "krlvrestriction.h"

namespace KRlv
{
    // Loads the persisted restriction table from the current avatar's
    // per-account directory. `table` is left untouched (caller keeps
    // whatever it already had) unless this returns true. A missing file
    // is normal (first login, or nothing was ever restricted) and returns
    // false without logging; a PRESENT file that fails its signature
    // check is logged loudly (LL_WARNS) and also returns false - a
    // tampered/corrupt file is discarded rather than trusted, but this
    // NEVER blocks viewer startup.
    bool loadRestrictions(KRlvRestrictionTable& table);

    // Persists `table`, signed, plus a secondary marker in gSavedSettings
    // (see the .cpp for why). Called after every addRestriction()/
    // removeRestriction() and once more from shutdown() - cheap enough
    // (tiny dataset, rare relative to frame rate) that no debouncing is
    // needed. Returns false if the write failed, the per-account directory
    // does not exist yet (not logged in), or no integrity hook is registered -
    // nothing is ever written unsigned. Never throws.
    bool saveRestrictions(const KRlvRestrictionTable& table);

    // KRLV Blacklist: a SEPARATE .dat file (krlv_blacklist.dat, not
    // commingled with krlv_restrictions.dat) - this is permanent,
    // user-side configuration (command NAMES the viewer refuses to ever
    // process, regardless of any object's restriction state), not
    // per-object restriction state, so it deliberately does not share
    // storage with the restriction table above even though both live in
    // the same per-account directory. Same tamper-evidence shape as
    // loadRestrictions()/saveRestrictions() - see krlv/README.md's
    // Blacklist section.
    bool loadBlacklist(std::unordered_set<std::string>& names);
    bool saveBlacklist(const std::unordered_set<std::string>& names);

    // KRLV Protected Debug Settings: a THIRD, separate .dat file
    // (krlv_protected_debug.dat) - the user's own permanent
    // configuration of WHICH Advanced > Debug Settings floater rows
    // @setdebug=n should lock from edits. The real RLVa spec describes
    // @setdebug as locking a small, undocumented whitelist - since that
    // whitelist's exact contents aren't in the public spec this module
    // is written from (clean-room), this viewer builds its own instead,
    // configured by the user rather than hardcoded or dictated by the
    // restricting object. Same shape and tamper-evidence as
    // loadBlacklist()/saveBlacklist() above, but names are NOT
    // case-folded - gSavedSettings control lookups are themselves
    // case-sensitive. See krlvviewercontrol.cpp's file header and
    // krlv/README.md's Viewer Control section.
    bool loadProtectedDebugSettings(std::unordered_set<std::string>& names);
    bool saveProtectedDebugSettings(const std::unordered_set<std::string>& names);
}

#endif // KRLV_KRLVPERSIST_H
