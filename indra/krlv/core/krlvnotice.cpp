/**
 * @file krlvnotice.cpp
 * @brief KRLV owner notices with flood protection - see krlvnotice.h.
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

#include "krlvnotice.h"

#include <chrono>
#include <map>
#include <vector>

#include "llerror.h"
#include "llcontrol.h"
#include "lluuid.h"
#include "krlvhandler.h"
#include "krlvowners.h"

// KRLV: gSavedSettings is the same extern every other krlv/core file relies on.
extern LLControlGroup gSavedSettings;

namespace
{
    // Floor on the interval, so the limit cannot be switched off by accident.
    constexpr float KRLV_NOTICE_MIN_INTERVAL = 5.f;
    constexpr float KRLV_NOTICE_DEFAULT_INTERVAL = 60.f;

    struct NoticeState
    {
        std::chrono::steady_clock::time_point lastSent{};
        bool everSent = false;
        int heldBack = 0;
    };

    std::map<KRlv::KRlvNoticeKind, NoticeState>& noticeStates()
    {
        static std::map<KRlv::KRlvNoticeKind, NoticeState> states;
        return states;
    }

    float noticeInterval()
    {
        float seconds = KRLV_NOTICE_DEFAULT_INTERVAL;
        if (gSavedSettings.controlExists("KRLVNoticeIntervalSeconds"))
        {
            seconds = gSavedSettings.getF32("KRLVNoticeIntervalSeconds");
        }
        return seconds < KRLV_NOTICE_MIN_INTERVAL ? KRLV_NOTICE_MIN_INTERVAL : seconds;
    }

    bool ownerWantsKind(const KRlv::KRlvOwner& owner, KRlv::KRlvNoticeKind kind)
    {
        switch (kind)
        {
        case KRlv::KRlvNoticeKind::Settings:    return owner.notifySettings;
        case KRlv::KRlvNoticeKind::Tamper:      return owner.notifyTamper;
        case KRlv::KRlvNoticeKind::Timestamp:   return owner.notifyTimestamp;
        case KRlv::KRlvNoticeKind::OwnerChange: return owner.notifyOwnerChanges;
        case KRlv::KRlvNoticeKind::Safeword:    return true;
        }
        return false;
    }
}

namespace KRlv
{
    bool sendOwnerNotice(KRlvNoticeKind kind, const std::string& text)
    {
        const auto now = std::chrono::steady_clock::now();
        NoticeState& state = noticeStates()[kind];
        const float interval = noticeInterval();

        if (kind != KRlvNoticeKind::Safeword && state.everSent)
        {
            const std::chrono::duration<float> since = now - state.lastSent;
            if (since.count() < interval)
            {
                // Held back. Counted, and summarised in the next notice of this kind.
                ++state.heldBack;
                return false;
            }
        }

        std::vector<KRlvOwner> owners;
        loadOwners(owners);

        std::string message = text;
        if (state.heldBack > 0)
        {
            message += " (" + std::to_string(state.heldBack) + " similar notice(s) were held back in the last "
                + std::to_string(static_cast<int>(interval)) + " seconds.)";
        }

        for (const KRlvOwner& owner : owners)
        {
            if (ownerWantsKind(owner, kind))
            {
                KRlvHandler::instance().requestSendIm(LLUUID(owner.uuid), message);
            }
        }

        state.lastSent = now;
        state.everSent = true;
        state.heldBack = 0;
        return true;
    }
}
