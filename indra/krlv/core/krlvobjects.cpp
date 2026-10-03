/**
 * @file krlvobjects.cpp
 * @brief KRLV object lists - see krlvobjects.h.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

#include "krlvobjects.h"

#include "llerror.h"
#include "lldate.h"
#include "lldir.h"
#include "llsd.h"
#include "krlvintegrity.h"

namespace
{
    constexpr S32 KRLV_OBJECTS_VERSION = 1;
    const std::string KRLV_OBJECTS_FILENAME = "krlv_objects.dat";
    const std::string KRLV_OBJECTS_DOMAIN = "objects";

    LLSD entriesToLLSD(const std::vector<KRlv::KRlvObjectEntry>& list)
    {
        LLSD entries = LLSD::emptyArray();
        for (const KRlv::KRlvObjectEntry& entry : list)
        {
            LLSD row;
            row["object"] = entry.object;
            row["owner"] = entry.owner;
            entries.append(row);
        }
        return entries;
    }

    void entriesFromLLSD(const LLSD& entries, std::vector<KRlv::KRlvObjectEntry>& list)
    {
        list.clear();
        for (LLSD::array_const_iterator it = entries.beginArray(); it != entries.endArray(); ++it)
        {
            KRlv::KRlvObjectEntry entry;
            entry.object = (*it)["object"].asString();
            entry.owner = (*it)["owner"].asString();
            list.push_back(entry);
        }
    }
}

namespace KRlv
{
    bool objectListed(const std::vector<KRlvObjectEntry>& list, const LLUUID& object, const LLUUID& owner)
    {
        const std::string object_text = object.asString();
        const std::string owner_text = owner.asString();
        for (const KRlvObjectEntry& entry : list)
        {
            if (entry.object == object_text && entry.owner == owner_text)
            {
                return true;
            }
        }
        return false;
    }

    bool objectAllowed(const KRlvObjectLists& lists, const LLUUID& object, const LLUUID& owner, std::string& reason)
    {
        if (objectListed(lists.blacklist, object, owner))
        {
            reason = "refused: object blacklisted";
            return false;
        }
        if (lists.mode == KRlvObjectMode::Closed && !objectListed(lists.whitelist, object, owner))
        {
            reason = "refused: object not whitelisted";
            return false;
        }
        return true;
    }

    bool saveObjectLists(const KRlvObjectLists& lists)
    {
        if (gDirUtilp->getLindenUserDir().empty())
        {
            return false;
        }

        LLSD root;
        root["version"] = KRLV_OBJECTS_VERSION;
        root["writtenAt"] = LLDate::now();
        root["mode"] = lists.mode == KRlvObjectMode::Closed ? std::string("closed") : std::string("open");
        root["whitelist"] = entriesToLLSD(lists.whitelist);
        root["blacklist"] = entriesToLLSD(lists.blacklist);

        if (!signDocument(root, KRLV_OBJECTS_DOMAIN))
        {
            return false;
        }

        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, KRLV_OBJECTS_FILENAME);
        if (!writeAtomically(path, root))
        {
            LL_WARNS("KRLV") << "Unable to write object lists to " << path << LL_ENDL;
            return false;
        }
        return true;
    }

    bool loadObjectLists(KRlvObjectLists& lists)
    {
        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, KRLV_OBJECTS_FILENAME);

        LLSD root;
        std::string status;
        if (!readDocument(path, root, status))
        {
            // "missing" is normal: open mode, no lists. "corrupt" is logged and ignored.
            if (status == "corrupt")
            {
                LL_WARNS("KRLV") << "KRLV object file " << path << " is unreadable - ignoring it." << LL_ENDL;
            }
            return false;
        }

        if (!root.has("signature"))
        {
            LL_WARNS("KRLV") << "Integrity incident: KRLV object file " << path << " is missing its signature - ignoring it." << LL_ENDL;
            return false;
        }

        if (!verifyDocument(root, KRLV_OBJECTS_DOMAIN))
        {
            LL_WARNS("KRLV") << "Integrity incident: KRLV object file " << path << " failed its integrity check - discarding it. "
                << "Open mode stays in effect. Startup is NOT blocked." << LL_ENDL;
            return false;
        }

        lists.mode = root["mode"].asString() == "closed" ? KRlvObjectMode::Closed : KRlvObjectMode::Open;
        entriesFromLLSD(root["whitelist"], lists.whitelist);
        entriesFromLLSD(root["blacklist"], lists.blacklist);
        LL_DEBUGS("KRLV") << "Loaded object lists from " << path << " (" << lists.whitelist.size()
            << " whitelisted, " << lists.blacklist.size() << " blacklisted)." << LL_ENDL;
        return true;
    }
}
