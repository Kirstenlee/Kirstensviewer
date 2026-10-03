/**
 * @file krlvpersist.cpp
 * @brief KRLV restriction-state persistence implementation.
 *
 * Design note (see krlv/README.md for the full write-up): this is
 * tamper-EVIDENCE, not tamper-PREVENTION. Anyone with filesystem access to
 * their own account folder can delete krlv_restrictions.dat outright, and
 * no client-side scheme can stop that - that is an acknowledged,
 * unavoidable limit, not something to oversell. What this DOES catch:
 * a casual hand-edit of the file's contents (the signature check fails
 * and the whole file is discarded, loudly, rather than trusted), and
 * silent full deletion (the gSavedSettings marker below is a second,
 * independent witness that restrictions were active as of the last clean
 * shutdown - written through LL's own normal settings save-on-quit path,
 * not by this file directly - so deleting only the .dat file no longer
 * erases every trace that something was active).
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

#include "krlvpersist.h"

#include "llerror.h"
#include "llcontrol.h"
#include "lldate.h"
#include "lldir.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "krlvintegrity.h"

// KRLV: gSavedSettings is the same extern every other krlv/core file
// (krlvhandler.cpp) already relies on - no new dependency.
extern LLControlGroup gSavedSettings;

namespace
{
    constexpr S32 KRLV_PERSIST_VERSION = 1;
    const std::string KRLV_PERSIST_FILENAME = "krlv_restrictions.dat";
    const std::string KRLV_BLACKLIST_FILENAME = "krlv_blacklist.dat";
    const std::string KRLV_PROTECTED_DEBUG_FILENAME = "krlv_protected_debug.dat";

    LLSD tableToLLSD(const KRlvRestrictionTable& table)
    {
        LLSD entries = LLSD::emptyArray();
        for (const auto& [behaviour, list] : table)
        {
            for (const auto& entry : list)
            {
                LLSD row;
                row["behaviour"] = behaviour;
                row["source"] = entry.sourceObjectId;
                row["option"] = entry.option;
                entries.append(row);
            }
        }
        return entries;
    }

    LLSD namesToLLSD(const std::unordered_set<std::string>& names)
    {
        LLSD entries = LLSD::emptyArray();
        for (const std::string& name : names)
        {
            entries.append(name);
        }
        return entries;
    }

    // Shared load path for the two name-list files.
    bool loadNameList(const std::string& filename, const std::string& domain, const std::string& label,
                      std::unordered_set<std::string>& names)
    {
        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, filename);
        if (!gDirUtilp->fileExists(path))
        {
            // Normal - nothing configured yet, or first login.
            return false;
        }

        LLSD root;
        std::string status;
        if (!KRlv::readDocument(path, root, status))
        {
            LL_WARNS("KRLV") << "KRLV " << label << " file " << path << " is " << status
                << " - ignoring its contents." << LL_ENDL;
            return false;
        }

        if (!root.has("signature") || !root.has(domain))
        {
            LL_WARNS("KRLV") << "Integrity incident: KRLV " << label << " file " << path
                << " is missing its signature - ignoring it." << LL_ENDL;
            return false;
        }

        if (!KRlv::verifyDocument(root, domain))
        {
            LL_WARNS("KRLV") << "Integrity incident: KRLV " << label << " file " << path
                << " failed its integrity check - discarding it. Startup is NOT blocked." << LL_ENDL;
            return false;
        }

        const LLSD& entries = root[domain];
        for (LLSD::array_const_iterator it = entries.beginArray(); it != entries.endArray(); ++it)
        {
            names.insert(it->asString());
        }

        LL_DEBUGS("KRLV") << "Loaded " << names.size() << " " << label << " entry(s) from " << path << LL_ENDL;
        return true;
    }

    // Shared save path for the two name-list files.
    bool saveNameList(const std::string& filename, const std::string& domain, const std::string& label,
                      const std::unordered_set<std::string>& names)
    {
        if (gDirUtilp->getLindenUserDir().empty())
        {
            return false;
        }

        LLSD root;
        root["version"] = KRLV_PERSIST_VERSION;
        root["writtenAt"] = LLDate::now();
        root[domain] = namesToLLSD(names);
        if (!KRlv::signDocument(root, domain))
        {
            return false;
        }

        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, filename);
        if (!KRlv::writeAtomically(path, root))
        {
            LL_WARNS("KRLV") << "Unable to write " << label << " state to " << path << LL_ENDL;
            return false;
        }
        return true;
    }
}

namespace KRlv
{
    bool saveRestrictions(const KRlvRestrictionTable& table)
    {
        if (gDirUtilp->getLindenUserDir().empty())
        {
            // Not logged in yet (or no per-account dir resolved) - not an
            // error, just nothing to persist to.
            return false;
        }

        LLSD root;
        root["version"] = KRLV_PERSIST_VERSION;
        root["writtenAt"] = LLDate::now();
        root["restrictions"] = tableToLLSD(table);
        if (!KRlv::signDocument(root, "restrictions"))
        {
            return false;
        }

        std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, KRLV_PERSIST_FILENAME);
        if (!writeAtomically(path, root))
        {
            LL_WARNS("KRLV") << "Unable to write restriction state to " << path << LL_ENDL;
            return false;
        }

        // S24: secondary, independent tamper-evidence signal - see the
        // file-level comment. Written through gSavedSettings' own normal
        // save-on-quit path (settings.xml), not opened directly here.
        gSavedSettings.setBOOL("KRLVRestrictionsWereActive", !table.empty());
        return true;
    }

    bool loadRestrictions(KRlvRestrictionTable& table)
    {
        std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, KRLV_PERSIST_FILENAME);
        if (!gDirUtilp->fileExists(path))
        {
            if (gSavedSettings.getBOOL("KRLVRestrictionsWereActive"))
            {
                LL_WARNS("KRLV") << "Integrity incident: KRLV restriction marker says restrictions were active as of the "
                    << "last clean shutdown, but " << path << " does not exist on this login. "
                    << "Startup is NOT blocked; affected objects will simply see their restrictions as no longer active." << LL_ENDL;
            }
            return false;
        }

        LLSD root;
        std::string status;
        if (!readDocument(path, root, status))
        {
            LL_WARNS("KRLV") << "KRLV restriction file " << path << " is " << status
                << " - ignoring its contents rather than guessing." << LL_ENDL;
            return false;
        }

        if (!root.has("signature") || !root.has("restrictions"))
        {
            LL_WARNS("KRLV") << "Integrity incident: KRLV restriction file " << path
                << " is missing its signature - ignoring it." << LL_ENDL;
            return false;
        }

        if (!KRlv::verifyDocument(root, "restrictions"))
        {
            LL_WARNS("KRLV") << "Integrity incident: KRLV restriction file " << path << " failed its integrity check - "
                << "discarding rather than trusting a possibly-altered restriction state. Startup is NOT blocked." << LL_ENDL;
            return false;
        }

        const LLSD& entries = root["restrictions"];
        for (LLSD::array_const_iterator it = entries.beginArray(); it != entries.endArray(); ++it)
        {
            const LLSD& row = *it;
            KRlvRestrictionEntry entry;
            entry.sourceObjectId = row["source"].asUUID();
            entry.option = row["option"].asString();
            table[row["behaviour"].asString()].push_back(entry);
        }

        LL_DEBUGS("KRLV") << "Loaded " << entries.size() << " persisted restriction(s) from " << path << LL_ENDL;
        return true;
    }

    bool saveBlacklist(const std::unordered_set<std::string>& names)
    {
        return saveNameList(KRLV_BLACKLIST_FILENAME, "blacklist", "blacklist", names);
    }

    bool loadBlacklist(std::unordered_set<std::string>& names)
    {
        return loadNameList(KRLV_BLACKLIST_FILENAME, "blacklist", "blacklist", names);
    }

    bool saveProtectedDebugSettings(const std::unordered_set<std::string>& names)
    {
        return saveNameList(KRLV_PROTECTED_DEBUG_FILENAME, "protectedDebug", "protected debug settings", names);
    }

    bool loadProtectedDebugSettings(std::unordered_set<std::string>& names)
    {
        return loadNameList(KRLV_PROTECTED_DEBUG_FILENAME, "protectedDebug", "protected debug settings", names);
    }
}
