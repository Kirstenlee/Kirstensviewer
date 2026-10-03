/**
 * @file krlvowners.cpp
 * @brief KRLV owner list persistence - see krlvowners.h.
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

#include "krlvowners.h"

#include <ctime>

#include "llerror.h"
#include "llcontrol.h"
#include "lldate.h"
#include "lldir.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "krlvintegrity.h"

// KRLV: gSavedSettings is the same extern every other krlv/core file relies on.
extern LLControlGroup gSavedSettings;

namespace
{
    constexpr S32 KRLV_OWNERS_VERSION = 2;
    const std::string KRLV_OWNERS_FILENAME = "krlv_owners.dat";
    const std::string KRLV_OWNERS_DOMAIN = "owners";

    LLSD ownerToLLSD(const KRlv::KRlvOwner& owner)
    {
        LLSD entry;
        entry["uuid"] = owner.uuid;
        entry["name"] = owner.name;
        entry["settings"] = owner.notifySettings;
        entry["tamper"] = owner.notifyTamper;
        entry["timestamp"] = owner.notifyTimestamp;
        entry["ownerchanges"] = owner.notifyOwnerChanges;
        return entry;
    }

    KRlv::KRlvOwner ownerFromLLSD(const LLSD& entry)
    {
        KRlv::KRlvOwner owner;
        owner.uuid = entry["uuid"].asString();
        owner.name = entry["name"].asString();
        owner.notifySettings = entry["settings"].asBoolean();
        owner.notifyTamper = entry["tamper"].asBoolean();
        owner.notifyTimestamp = entry["timestamp"].asBoolean();
        owner.notifyOwnerChanges = entry["ownerchanges"].asBoolean();
        return owner;
    }
}

namespace KRlv
{
    bool saveOwners(const std::vector<KRlvOwner>& owners)
    {
        if (gDirUtilp->getLindenUserDir().empty())
        {
            return false;
        }

        LLSD entries = LLSD::emptyArray();
        for (const KRlvOwner& owner : owners)
        {
            entries.append(ownerToLLSD(owner));
        }

        // Written-at epoch, used by the tamper checks for rollback and future-dating.
        const double written_epoch = static_cast<double>(std::time(nullptr));

        LLSD root;
        root["version"] = KRLV_OWNERS_VERSION;
        root["writtenAt"] = LLDate::now();
        root["writtenEpoch"] = written_epoch;
        root["owners"] = entries;

        if (!signDocument(root, KRLV_OWNERS_DOMAIN))
        {
            return false;
        }

        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, KRLV_OWNERS_FILENAME);
        if (!writeAtomically(path, root))
        {
            LL_WARNS("KRLV") << "Unable to write owner state to " << path << LL_ENDL;
            return false;
        }

        // Deletion marker: lets a later missing file be spotted as tampering.
        gSavedSettings.setBOOL("KRLVOwnersWereSet", !owners.empty());
        // Newest written-at seen by this viewer: a restored older copy is then detectable.
        // Stored as text: this settings group has no 64-bit setter, and a 32-bit float
        // cannot hold a unix time to the second.
        gSavedSettings.setString("KRLVOwnersWrittenEpoch", std::to_string(static_cast<long long>(written_epoch)));
        return true;
    }

    bool loadOwners(std::vector<KRlvOwner>& owners)
    {
        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, KRLV_OWNERS_FILENAME);

        LLSD root;
        std::string status;
        if (!readDocument(path, root, status))
        {
            // "missing" is normal when no owners were ever set. "corrupt" is logged.
            if (status == "corrupt")
            {
                LL_WARNS("KRLV") << "KRLV owner file " << path << " is unreadable or not valid - ignoring it." << LL_ENDL;
            }
            return false;
        }

        if (!root.has("signature") || !root.has("owners"))
        {
            LL_WARNS("KRLV") << "Integrity incident: KRLV owner file " << path << " is missing its signature - ignoring it." << LL_ENDL;
            return false;
        }

        if (!verifyDocument(root, KRLV_OWNERS_DOMAIN))
        {
            LL_WARNS("KRLV") << "Integrity incident: KRLV owner file " << path
                << " failed its signature check - discarding it. Startup is NOT blocked." << LL_ENDL;
            return false;
        }

        owners.clear();
        const LLSD& entries = root["owners"];
        for (LLSD::array_const_iterator it = entries.beginArray(); it != entries.endArray(); ++it)
        {
            owners.push_back(ownerFromLLSD(*it));
        }

        LL_DEBUGS("KRLV") << "Loaded " << owners.size() << " owner(s) from " << path << LL_ENDL;
        return true;
    }
}
