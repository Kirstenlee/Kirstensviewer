/**
 * @file krlvsitting.cpp
 * @brief KRLV "Sitting" category - @sit, @unsit, @sitground, @getsitid.
 *
 * COMPLETE: all 4 command names. @sit blocks ALL sitting (including its
 * own @sit:<UUID>=force syntax, and @sittp's distance limit from the
 * Teleportation category) by gating the single real chokepoint,
 * handle_object_sit() (llviewermenu.cpp) - both the ordinary sit path
 * and @sit:<UUID>=force's hook route through it, so one gate covers
 * both automatically. @unsit gates LLAgent::standUp() the same way, so
 * @unsit=force naturally inherits the same restriction (matches the
 * pattern already used for @tploc/@tpto in Teleportation). @sitground
 * gates LLAgent::sitDown(), matching the spec's OWN explicit statement
 * that it "will fail if the avatar is under a @sit restriction".
 *
 * Not implemented: hiding the "Stand Up" button the spec mentions -
 * clicking it while @unsit is restricted already does nothing (the
 * restriction is enforced at the function level, not the button), so
 * this would be cosmetic polish only, not a functional gap.
 *
 * @sit:<UUID>=force does NOT separately check the spec's "prevented from
 * unsitting" clause - in practice this has no independent effect beyond
 * what the @unsit gate on standUp() already causes server-side, so no
 * extra logic was added for it.
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

// KRLV: same plain external-linkage free function krlvversion.cpp
// forward-declares locally.
void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

namespace
{
    void applyToggle(const std::string& restrictionKey, const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        if (cmd.param == "n")
        {
            gKRlv.addRestriction(restrictionKey, sourceId, cmd.option);
        }
        else if (cmd.param == "y")
        {
            gKRlv.removeRestriction(restrictionKey, sourceId);
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

void krlv_register_sitting_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("sit",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.hasOption())
            {
                // "@sit:<UUID>=force" - one-shot, routes through the
                // same gated chokepoint as an ordinary sit (see file
                // header).
                LLUUID target;
                if (target.set(cmd.option, false) && target.notNull())
                {
                    gKRlv.requestForceSit(target);
                }
            }
            else
            {
                // "@sit=<y/n>"
                applyToggle("sit", cmd, sourceId);
            }
            return true;
        });

    handler.registerBehaviour("unsit",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                gKRlv.requestForceStand();
            }
            else
            {
                // "@unsit=<y/n>"
                applyToggle("unsit", cmd, sourceId);
            }
            return true;
        });

    handler.registerBehaviour("sitground",
        [](const KRlvCommand&, const LLUUID&, const LLUUID&) -> bool
        {
            gKRlv.requestForceSitGround();
            return true;
        });

    handler.registerBehaviour("getsitid",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            LLUUID sitId = gKRlv.requestGetSitId();
            replyOnChannel(sitId.isNull() ? "NULL_KEY" : sitId.asString(), cmd.param);
            return true;
        });
}
