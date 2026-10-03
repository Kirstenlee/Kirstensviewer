/**
 * @file krlvowners.h
 * @brief KRLV owner list persistence - the people (by UUID) who are notified
 * of settings access/changes, state-file tampering, timestamp mismatches and
 * owner-list changes. Stored in its own signed file, krlv_owners.dat, using
 * the same tamper-evidence shape as krlvpersist.cpp's other state files.
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

#ifndef KRLV_KRLVOWNERS_H
#define KRLV_KRLVOWNERS_H

#include <string>
#include <vector>

namespace KRlv
{
    // One owner. The UUID is the identity; the name is a display label captured
    // when the owner was added. Each notice type can be switched per owner.
    struct KRlvOwner
    {
        std::string uuid;
        std::string name;
        bool notifySettings = true;       // KRLV or protected settings accessed/changed
        bool notifyTamper = true;         // a state file corrupted or deleted
        bool notifyTimestamp = true;      // timestamp mismatch between state files
        bool notifyOwnerChanges = true;   // owner added or removed
    };

    // Loads the owner list from the current avatar's per-account directory.
    // Returns false (and leaves `owners` untouched) on a missing or
    // tamper-failed file. A missing file is normal (no owners configured).
    bool loadOwners(std::vector<KRlvOwner>& owners);

    // Saves the owner list, signed. Also records whether any owners were set in
    // gSavedSettings ("KRLVOwnersWereSet") so a later deletion of the file can be
    // detected. Returns false only if the write itself failed.
    bool saveOwners(const std::vector<KRlvOwner>& owners);
}

#endif // KRLV_KRLVOWNERS_H
