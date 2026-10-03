/**
 * @file krlvgroup.cpp
 * @brief KRLV "Group" category - @setgroup, @getgroup.
 *
 * COMPLETE: both command names, no stubs.
 *
 * @setgroup:<name>=force resolves a group name against the agent's own
 * membership list (LLAgent::mGroups) and calls LLGroupActions::activate()
 * - confirmed via investigation to be the SAME function every real
 * "activate this group" UI path already uses (the Groups floater, the
 * People/Conversations group-session list, the group directory search
 * panel, the group info panel's own "Activate" button - 5 real call
 * sites, one shared function). @setgroup=<y/n> gates that exact same
 * function directly, so the force form naturally inherits the
 * restriction too - the same "gate the shared function, force inherits
 * it for free" pattern already proven for @sit:<UUID>=force/@tpto=force.
 * The literal group name "none" deactivates the current group entirely
 * (LLGroupActions::activate(LLUUID::null)), matching the spec's own
 * stated special case.
 *
 * @getgroup is a genuinely LIVE read (like @getsitid/@getcam_fov, not a
 * stored-restriction echo) - LLAgent::getGroupName() already returns
 * the current active group's name directly, "none" being LLAgent's own
 * convention for "no group active" as well (confirmed matching, no
 * special-case code needed on the read side).
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
#include "lluuid.h"

#include "krlvhandler.h"

// KRLV: same plain external-linkage free function several sibling files
// forward-declare locally.
void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

namespace
{
    void applyToggle(const std::string& key, const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        if (cmd.param == "n")
        {
            gKRlv.addRestriction(key, sourceId, cmd.option);
        }
        else if (cmd.param == "y")
        {
            gKRlv.removeRestriction(key, sourceId);
        }
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
            return;
        }
        send_chat_from_viewer(text, CHAT_TYPE_SHOUT, channel);
    }
}

void krlv_register_group_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("setgroup",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                gKRlv.requestForceSetGroup(cmd.option);
            }
            else
            {
                applyToggle("setgroup", cmd, sourceId);
            }
            return true;
        });

    handler.registerBehaviour("getgroup",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(gKRlv.requestGetGroupName(), cmd.param);
            return true;
        });
}
