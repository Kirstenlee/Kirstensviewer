/**
 * @file krlvversion.cpp
 * @brief KRLV "Version Checking" category - @version, @versionnew,
 * @versionnum.
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

#include "krlvbehaviours.h"

#include <cstdlib>
#include <string>

#include "llerror.h"
#include "llchat.h"

#include "krlvversion.h"
#include "krlvhandler.h"

// KRLV: send_chat_from_viewer() is a plain external-linkage free function
// (llfloaterimnearbychat.cpp) with no header of its own - forward-declared
// locally here rather than exporting a new header from newview, so this
// stays the zero-touch outbound path the architecture plan called for.
void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

namespace
{
    constexpr int KRLV_VERSION_MAJOR = 1;
    constexpr int KRLV_VERSION_MINOR = 0;
    constexpr int KRLV_VERSION_PATCH = 0;

    std::string krlvVersionString()
    {
        return "KRLV v" + std::to_string(KRLV_VERSION_MAJOR) + "."
            + std::to_string(KRLV_VERSION_MINOR) + "."
            + std::to_string(KRLV_VERSION_PATCH)
            + " (clean-room RestrainedLove API implementation, S24)";
    }

    void replyOnChannel(const std::string& text, const std::string& channelParam)
    {
        if (channelParam.empty())
        {
            return;
        }
        const S32 channel = std::atoi(channelParam.c_str());
        if (channel == 0)
        {
            // RLVa spec: "Always use a non-zero integer" - a script that
            // asks for channel 0 gets no reply, same as every other
            // RLV-capable viewer (channel 0 is the public chat channel).
            return;
        }
        // Shout range (100m), not say (10m): a relay object worn
        // elsewhere on the same avatar is always in range regardless, but
        // this keeps a reply reaching a detached/positioned prop too.
        send_chat_from_viewer(text, CHAT_TYPE_SHOUT, channel);
    }
}

namespace KRlv
{
    std::string getVersionNumber()
    {
        // A single incrementing integer, same shape scripts expect for a
        // numeric >= comparison - the exact encoding is KRLV's own, there
        // is no prior KRLV version number for any script to compare
        // against yet.
        return std::to_string(KRLV_VERSION_MAJOR * 1000000
            + KRLV_VERSION_MINOR * 10000
            + KRLV_VERSION_PATCH * 100);
    }

    std::string krlvVersionText()
    {
        return std::to_string(KRLV_VERSION_MAJOR) + "."
            + std::to_string(KRLV_VERSION_MINOR) + "."
            + std::to_string(KRLV_VERSION_PATCH);
    }

    std::string rlvApiVersionText()
    {
        // Latest RLV API version listed in the KRLV tracker.
        return "2.9.29";
    }
}

void krlv_register_version_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("version",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(krlvVersionString(), cmd.param);
            return true;
        });

    handler.registerBehaviour("versionnew",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(krlvVersionString(), cmd.param);
            return true;
        });

    handler.registerBehaviour("versionnum",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(KRlv::getVersionNumber(), cmd.param);
            return true;
        });

    // "@version" sent bare, in IM from an avatar (not chat from an
    // object) - replies via IM instead of a chat-channel reply, same
    // content as the chat form. See krlvhandler.h's
    // KRlvManualImReplyBuilder/processInboundIM() and krlv/README.md's
    // "IM subsystem" section.
    handler.registerManualImCommand("version", krlvVersionString);
}
