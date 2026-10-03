/**
 * @file krlvblacklist.cpp
 * @brief KRLV "Blacklist handling" category - @versionnumbl,
 * @getblacklist.
 *
 * The blacklist itself - a PERMANENT, user-configured set of RLVa
 * command names the viewer refuses to process from ANY object,
 * regardless of any active restriction state - is core KRLV
 * infrastructure, not something these two query commands define: see
 * KRlvHandler::isBlacklisted()/addToBlacklist()/removeFromBlacklist()
 * (krlvhandler.h/.cpp), enforced once in processInboundChat() before
 * any handler runs, persisted to its own signed file
 * (krlv_blacklist.dat, krlvpersist.h/.cpp, deliberately separate from
 * krlv_restrictions.dat per the user's own explicit request). It is
 * configured ONLY via the S24 Console ("krlv blacklist [add|remove
 * <name>]") - there is no RLVa command anywhere in the spec that lets an
 * OBJECT add to it, which would defeat the entire point of a permanent
 * user-side override.
 *
 * COMPLETE: @versionnumbl, the chat-channel form of @getblacklist, AND
 * now @getblacklist's bare-syntax manual form ("@getblacklist" with no
 * `=`, sent via IM from an avatar rather than object chat, answered
 * stealthily via IM rather than a chat-channel reply) - registered via
 * KRlvHandler::registerManualImCommand()/processInboundIM()
 * (krlvhandler.h/.cpp), the same general-purpose "manual IM command"
 * mechanism @version's own manual form uses (krlvversion.cpp). Built
 * alongside a genuinely reusable IM-SEND primitive
 * (KRlvSendImRequest/requestSendIm(), krlvhandler.h) - not a one-off
 * hack for just these two commands - intended as the foundation for a
 * later owner-alert/tamper-notification feature; see krlv/README.md's
 * "IM subsystem" section.
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

#include "llerror.h"
#include "llchat.h"

#include "krlvversion.h"
#include "krlvhandler.h"

// KRLV: same plain external-linkage free function several sibling files
// forward-declare locally.
void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

namespace
{
    void replyOnChannel(const std::string& text, const std::string& channelParam)
    {
        if (channelParam.empty())
        {
            return;
        }
        const S32 channel = std::atoi(channelParam.c_str());
        if (channel == 0)
        {
            return;
        }
        send_chat_from_viewer(text, CHAT_TYPE_SHOUT, channel);
    }

    std::string joinBlacklist(const std::string& filter)
    {
        std::string joined;
        for (const std::string& name : gKRlv.getBlacklist())
        {
            if (!filter.empty() && name.find(filter) == std::string::npos)
            {
                continue;
            }
            if (!joined.empty())
            {
                joined += ",";
            }
            joined += name;
        }
        return joined;
    }
}

void krlv_register_blacklist_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("versionnumbl",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(KRlv::getVersionNumber() + "," + joinBlacklist(std::string()), cmd.param);
            return true;
        });

    handler.registerBehaviour("getblacklist",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(joinBlacklist(cmd.option), cmd.param);
            return true;
        });

    // "@getblacklist" sent bare, in IM from an avatar (not chat from an
    // object) - no filter option exists in this form (the manual IM
    // syntax carries no parameters at all), so this is always the
    // unfiltered full list, matching the chat form's own
    // joinBlacklist(std::string()) call for the equivalent unfiltered
    // case.
    handler.registerManualImCommand("getblacklist", []() { return joinBlacklist(std::string()); });
}
