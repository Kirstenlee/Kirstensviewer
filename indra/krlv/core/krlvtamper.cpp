/**
 * @file krlvtamper.cpp
 * @brief KRLV tamper detection with hysteresis - see krlvtamper.h.
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

#include "krlvtamper.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <set>

#include <boost/bind/bind.hpp>
#include <boost/signals2.hpp>

#include "llerror.h"
#include "llcallbacklist.h"
#include "llcontrol.h"
#include "lldir.h"
#include "llsd.h"
#include "krlvintegrity.h"
#include "krlvnotice.h"
#include "krlvowners.h"
#include "krlvstatus.h"
#include "krlvhandler.h"

// KRLV: gSavedSettings is the same extern every other krlv/core file relies on.
extern LLControlGroup gSavedSettings;

namespace
{
    // A stamp this far out of step is not a clock glitch, a DST change or a second of
    // drift: it is treated as a real timestamp problem. Anything inside it is ignored.
    constexpr double KRLV_TIME_TOLERANCE_SECONDS = 300.0;

    // Confirmation delay: a problem must still be there this long after the first look.
    constexpr float KRLV_CONFIRM_DELAY_SECONDS = 60.f;
    constexpr float KRLV_FIRST_LOOK_SECONDS = 5.f;

    // Status for the Control tab. Set by startTamperChecks() and the timed observations.
    bool sTamperActive = false;
    std::string sLastCheckText;

    // Bumped on every startTamperChecks(); a delayed callback from an earlier start does nothing.
    unsigned sTamperGeneration = 0;

    // Incidents notified during this session. Not persisted.
    std::set<std::string> sSessionNotified;

    // Upper bound for the persisted notified-key list.
    constexpr size_t KRLV_TAMPER_NOTIFIED_KEYS_MAX = 64;

    struct Incident
    {
        std::string file;
        std::string kind;   // deleted, signature, rollback, timestamp
        std::string key;    // stable identity of this incident, used for dedupe
        bool timestampIssue() const { return kind == "rollback" || kind == "timestamp"; }
    };

    std::string ownersPath()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "krlv_owners.dat");
    }

    double nowSeconds()
    {
        return static_cast<double>(std::time(nullptr));
    }

    // Looks at the owner file and reports any incidents present right now.
    std::vector<Incident> observeOwners()
    {
        std::vector<Incident> found;
        const std::string path = ownersPath();

        LLSD root;
        std::string status;
        KRlv::readDocument(path, root, status);

        if (status == "missing")
        {
            // Only a deletion if the settings say owners were set before. A fresh account
            // with no owners is normal.
            if (gSavedSettings.getBOOL("KRLVOwnersWereSet"))
            {
                found.push_back({ "owners", "deleted", "owners:deleted" });
            }
            return found;
        }

        if (status == "corrupt")
        {
            found.push_back({ "owners", "signature", "owners:corrupt" });
            return found;
        }

        // The file is present and parses: verify its signature the same way the loader does.
        std::vector<KRlv::KRlvOwner> owners;
        if (!KRlv::loadOwners(owners))
        {
            found.push_back({ "owners", "signature", "owners:signature" });
            return found;
        }

        // Timestamps: compare the file's own written time with the newest one recorded.
        if (root.has("writtenEpoch"))
        {
            const double written = root["writtenEpoch"].asReal();
            const double recorded = std::strtod(gSavedSettings.getString("KRLVOwnersWrittenEpoch").c_str(), nullptr);
            const double now = nowSeconds();

            if (recorded > 0.0 && written < recorded - KRLV_TIME_TOLERANCE_SECONDS)
            {
                // An older copy was put back in place of a newer one.
                found.push_back({ "owners", "rollback", "owners:rollback:" + std::to_string(static_cast<long long>(written)) });
            }
            else if (written > now + KRLV_TIME_TOLERANCE_SECONDS)
            {
                // Written in the future, beyond any tolerable clock difference.
                found.push_back({ "owners", "timestamp", "owners:timestamp:" + std::to_string(static_cast<long long>(written)) });
            }
        }
        return found;
    }

    // Stored oldest first, separated by '|'.
    std::vector<std::string> storedNotifiedKeys()
    {
        std::vector<std::string> keys;
        const std::string stored = gSavedSettings.getString("KRLVTamperNotifiedKeys");
        std::string current;
        for (char c : stored)
        {
            if (c == '|')
            {
                if (!current.empty())
                {
                    keys.push_back(current);
                }
                current.clear();
            }
            else
            {
                current += c;
            }
        }
        if (!current.empty())
        {
            keys.push_back(current);
        }
        return keys;
    }

    // Persisted keys plus this session's keys - the dedupe set for confirmAndNotify().
    std::set<std::string> notifiedKeys()
    {
        std::set<std::string> keys = sSessionNotified;
        for (const std::string& k : storedNotifiedKeys())
        {
            keys.insert(k);
        }
        return keys;
    }

    // Keeps only the newest KRLV_TAMPER_NOTIFIED_KEYS_MAX stored keys.
    void rememberNotified(const std::string& key)
    {
        sSessionNotified.insert(key);

        std::vector<std::string> keys = storedNotifiedKeys();
        keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
        keys.push_back(key);
        if (keys.size() > KRLV_TAMPER_NOTIFIED_KEYS_MAX)
        {
            keys.erase(keys.begin(), keys.end() - KRLV_TAMPER_NOTIFIED_KEYS_MAX);
        }

        std::string joined;
        for (const std::string& k : keys)
        {
            joined += k + "|";
        }
        gSavedSettings.setString("KRLVTamperNotifiedKeys", joined);
    }

    // ---- Settings-change notices -------------------------------------------------
    // Reads cannot be watched through the settings system, so only changes are reported.

    std::vector<boost::signals2::connection> sSettingConnections;
    std::map<std::string, std::string> sLastSettingValue;   // raw values, memory only

    // Hashes, salts, keys and PINs: a change is reported, but the value never is.
    bool isSecretSetting(const std::string& name)
    {
        return name.find("Hash") != std::string::npos
            || name.find("Salt") != std::string::npos
            || name.find("Key") != std::string::npos
            || name.find("Pin") != std::string::npos;
    }

    void onWatchedSettingChanged(const std::string& name)
    {
        LLControlVariable* control = gSavedSettings.getControl(name);
        if (!control)
        {
            return;
        }

        const std::string raw = control->getValue().asString();
        auto previous = sLastSettingValue.find(name);
        if (previous != sLastSettingValue.end() && previous->second == raw)
        {
            return;   // set to the value it already had - not a change
        }
        sLastSettingValue[name] = raw;

        const std::string shown = isSecretSetting(name) ? std::string("(value withheld)") : raw;
        const std::string text = "KRLV settings changed: " + name + " = " + shown
            + " at " + std::to_string(static_cast<long long>(nowSeconds())) + " (unix time).";
        LL_DEBUGS("KRLV") << "Watched setting changed: " << name << LL_ENDL;
        KRlv::sendOwnerNotice(KRlv::KRlvNoticeKind::Settings, text);
    }

    void watchSetting(const std::string& name)
    {
        LLControlVariable* control = gSavedSettings.getControl(name);
        if (!control)
        {
            return;
        }
        sLastSettingValue[name] = control->getValue().asString();
        sSettingConnections.push_back(
            control->getCommitSignal()->connect(boost::bind(&onWatchedSettingChanged, name)));
    }

    void watchSettings()
    {
        // KRLV's own settings: the enable switch, the safeword and PIN hashes, the integrity
        // key, and the lock and notice timings.
        const char* krlvSettings[] = {
            "KRLVEnabled",
            "KRLVControlEnabled",
            "KRLVControlLockSeconds",
            "KRLVNoticeIntervalSeconds",
            "KRLVControlPinHash",
            "KRLVSafewordHash",
            "KRLVIntegrityKey",
        };
        for (const char* name : krlvSettings)
        {
            watchSetting(name);
        }

        // Every protected debug setting the wearer has listed.
        for (const std::string& name : KRlvHandler::instance().getProtectedDebugSettings())
        {
            watchSetting(name);
        }
    }

    std::string describe(const Incident& incident)
    {
        if (incident.kind == "deleted")
        {
            return "the owner list file was deleted";
        }
        if (incident.kind == "signature")
        {
            return "the owner list file failed its integrity check";
        }
        if (incident.kind == "rollback")
        {
            return "an older copy of the owner list was restored";
        }
        return "the owner list file is dated in the future";
    }

    // Reports only incidents seen in BOTH observations, and only once each.
    void confirmAndNotify(const std::vector<Incident>& first, const std::vector<Incident>& second)
    {
        const std::set<std::string> alreadyNotified = notifiedKeys();
        for (const Incident& a : first)
        {
            bool stillPresent = false;
            for (const Incident& b : second)
            {
                if (a.key == b.key)
                {
                    stillPresent = true;
                    break;
                }
            }
            if (!stillPresent || alreadyNotified.count(a.key) > 0)
            {
                continue;
            }

            const std::string text = "KRLV Control integrity notice: " + describe(a) + ". Detected " + std::to_string(static_cast<long long>(nowSeconds())) + " (unix time).";
            const KRlv::KRlvNoticeKind kind = a.timestampIssue() ? KRlv::KRlvNoticeKind::Timestamp : KRlv::KRlvNoticeKind::Tamper;
            LL_WARNS("KRLV") << "Integrity incident confirmed: " << a.key << LL_ENDL;
            if (KRlv::sendOwnerNotice(kind, text))
            {
                rememberNotified(a.key);
            }
            // If held back by the flood limit, it is not remembered, so it is retried on
            // the next confirmation rather than being lost.
        }
    }
}

namespace KRlv
{
    void startTamperChecks()
    {
        // Restart-safe: drop the watches from any earlier start, then take a new generation
        // so timers still pending from that start become no-ops.
        for (boost::signals2::connection& connection : sSettingConnections)
        {
            connection.disconnect();
        }
        sSettingConnections.clear();
        const unsigned generation = ++sTamperGeneration;

        sTamperActive = true;
        watchSettings();

        // Two observations 60 seconds apart. The first is shared with the confirmation
        // step through a shared pointer, so the closure owns its own copy.
        auto first = std::make_shared<std::vector<Incident>>();
        doAfterInterval([first, generation]()
        {
            if (generation != sTamperGeneration)
            {
                return;
            }
            *first = observeOwners();
            sLastCheckText = std::to_string(static_cast<long long>(nowSeconds()));
            doAfterInterval([first, generation]()
            {
                if (generation != sTamperGeneration)
                {
                    return;
                }
                confirmAndNotify(*first, observeOwners());
                sLastCheckText = std::to_string(static_cast<long long>(nowSeconds()));
            }, KRLV_CONFIRM_DELAY_SECONDS);
        }, KRLV_FIRST_LOOK_SECONDS);
    }

    bool tamperChecksActive()
    {
        return sTamperActive;
    }

    std::string tamperLastCheckText()
    {
        return sLastCheckText.empty() ? std::string("not yet checked") : sLastCheckText + " (unix time)";
    }
}
