/**
 * @file krlvobjects.h
 * @brief KRLV object lists: an object whitelist and blacklist, matched on the object
 * and its owner together, plus an open or closed mode. Stored in its own signed file,
 * krlv_objects.dat, with the same keyed signature and atomic writes as the other state
 * files.
 *
 * Open mode (the default): objects on the blacklist are refused, everything else is
 * heard. Closed mode: only objects on the whitelist are heard. The safeword and the
 * command states (ACTIVE / DISABLED / SCRIPT) still apply in both modes.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
 *
 * KRLV is a clean-room implementation, written entirely from the public
 * RLVa API specification, with respect and thanks to Marine Kelley and Kitty
 * Barnett. No code from their viewers was read, copied or referenced.
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

#ifndef KRLV_KRLVOBJECTS_H
#define KRLV_KRLVOBJECTS_H

#include <string>
#include <vector>

#include "lluuid.h"

namespace KRlv
{
    enum class KRlvObjectMode
    {
        Open,    // default: hear everything except blacklisted objects
        Closed   // hear only whitelisted objects
    };

    // One listed object. An entry matches only when both the object ID and the owner ID
    // agree with the incoming command's source, so one owner's other objects are not
    // trusted by accident. A re-rezzed copy gets a new object ID and needs a new entry.
    struct KRlvObjectEntry
    {
        std::string object;
        std::string owner;
    };

    struct KRlvObjectLists
    {
        KRlvObjectMode mode = KRlvObjectMode::Open;
        std::vector<KRlvObjectEntry> whitelist;
        std::vector<KRlvObjectEntry> blacklist;
    };

    // True when the object and owner pair appears in `list`.
    bool objectListed(const std::vector<KRlvObjectEntry>& list, const LLUUID& object, const LLUUID& owner);

    // The decision: returns true if commands from this source may be heard. On refusal,
    // `reason` says why, for the command log.
    bool objectAllowed(const KRlvObjectLists& lists, const LLUUID& object, const LLUUID& owner, std::string& reason);

    bool loadObjectLists(KRlvObjectLists& lists);
    bool saveObjectLists(const KRlvObjectLists& lists);
}

#endif // KRLV_KRLVOBJECTS_H
