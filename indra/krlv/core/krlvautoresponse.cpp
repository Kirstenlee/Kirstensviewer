/**
 * @file krlvautoresponse.cpp
 * @brief KRLV auto-response for dropped IM and chat - see krlvautoresponse.h.
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

#include "krlvautoresponse.h"

#include <chrono>
#include <map>
#include <string>

#include "llerror.h"
#include "llcontrol.h"
#include "krlvhandler.h"

// KRLV: gSavedSettings is the same extern every other krlv/core file relies on.
extern LLControlGroup gSavedSettings;

namespace
{
    // Floor on the cooldown, so a reply can never be set to repeat in a tight loop.
    constexpr float KRLV_AUTORESPONSE_MIN_COOLDOWN = 60.f;
    constexpr float KRLV_AUTORESPONSE_DEFAULT_COOLDOWN = 600.f;

    using Clock = std::chrono::steady_clock;

    // Last reply time per sender, separately for IM and chat, held in memory only.
    std::map<std::string, Clock::time_point>& imLastReply()
    {
        static std::map<std::string, Clock::time_point> sent;
        return sent;
    }

    std::map<std::string, Clock::time_point>& chatLastReply()
    {
        static std::map<std::string, Clock::time_point> sent;
        return sent;
    }

    float cooldownSeconds()
    {
        float seconds = KRLV_AUTORESPONSE_DEFAULT_COOLDOWN;
        if (gSavedSettings.controlExists("KRLVAutoResponseCooldownSeconds"))
        {
            seconds = gSavedSettings.getF32("KRLVAutoResponseCooldownSeconds");
        }
        return seconds < KRLV_AUTORESPONSE_MIN_COOLDOWN ? KRLV_AUTORESPONSE_MIN_COOLDOWN : seconds;
    }

    // True when this sender has not been answered within the cooldown. Records the reply
    // when it returns true, so a sender is answered at most once per cooldown.
    bool claimReply(std::map<std::string, Clock::time_point>& sent, const std::string& key)
    {
        const Clock::time_point now = Clock::now();
        auto previous = sent.find(key);
        if (previous != sent.end())
        {
            const std::chrono::duration<float> since = now - previous->second;
            if (since.count() < cooldownSeconds())
            {
                return false;
            }
        }
        sent[key] = now;
        return true;
    }

    void reply(const LLUUID& fromId, const std::string& text)
    {
        if (text.empty())
        {
            return;
        }
        KRlvHandler::instance().requestSendIm(fromId, text);
    }
}

namespace KRlv
{
    void autoRespondDroppedIM(const LLUUID& fromId)
    {
        if (fromId.isNull() || !KRlvHandler::instance().isEnabled())
        {
            return;
        }
        if (!claimReply(imLastReply(), fromId.asString()))
        {
            return;
        }
        reply(fromId, gSavedSettings.getString("KRLVAutoResponseIMText"));
    }

    void autoRespondDroppedChat(const LLUUID& fromId)
    {
        if (fromId.isNull() || !KRlvHandler::instance().isEnabled())
        {
            return;
        }
        if (!claimReply(chatLastReply(), fromId.asString()))
        {
            return;
        }
        // Chat is answered by IM, since the chat line itself was dropped.
        reply(fromId, gSavedSettings.getString("KRLVAutoResponseChatText"));
    }
}
