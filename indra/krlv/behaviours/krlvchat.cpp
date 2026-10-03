/**
 * @file krlvchat.cpp
 * @brief KRLV "Chat, Emotes and Instant Messages" category.
 *
 * COMPLETE: all 25 of 25 command names, each a single real chokepoint
 * (see krlv/README.md's Touch Points table) -
 * @sendchat/@emote/@redirchat/@rediremote, @chatshout/@chatnormal/
 * @chatwhisper, @recvchat/@recvchat_sec/@recvchatfrom, @sendgesture,
 * @recvemote/@recvemote_sec/@recvemotefrom,
 * @sendchannel/@sendchannel_sec/@sendchannel_except, @sendim/
 * @sendim_sec/@sendimto, @startim/@startimto, @recvim/@recvim_sec/
 * @recvimfrom.
 *
 * @sendchat/@emote/@redirchat/@rediremote implement the REAL spec text
 * in full (an earlier pass had @sendchat as a blanket block with the
 * other 3 left as stubs - revisited since @emote/@redirchat/@rediremote
 * only make sense against the real rule, not a blanket block):
 * - Plain chat (no leading '/' at all) is discarded outright when
 *   @sendchat is restricted - nothing to let through.
 * - Emotes ("/me "/"/me'", the same IRC-style prefix check
 *   llfloaterimnearbychat.cpp's own sendChat() already uses for chat
 *   bubbles) and other "/"-prefixed messages go through TRUNCATED (30
 *   and 15 characters respectively) via KRlv::filterSendChatText()
 *   (krlvchat.h/.cpp) - unless @emote=add is active, which is the
 *   documented exception to LENGTH truncation specifically, not to the
 *   rules below.
 * - A literal '.' truncates the rest of the message from that point,
 *   and any of the special characters `()"-*=_^` discard the WHOLE
 *   message outright - both apply regardless of @emote's exception
 *   state (spec's own wording: "special signs will still discard the
 *   message").
 * - @redirchat (plain chat)/@rediremote (emotes) reroute the message to
 *   one or more private channels INSTEAD of the public channel
 *   entirely - checked first, independent of @sendchat's own
 *   restriction state (the spec never ties them together; the only
 *   stated cross-command interaction is "@redirchat does not supersede
 *   @sendchannel", a different command). Sent via the same
 *   send_chat_from_viewer() free-function primitive this whole category
 *   already uses for private-channel replies - no new send mechanism
 *   needed. "If several redirections are issued, the message is
 *   redirected to each channel" (spec's own wording) - `getRestrictionOptions()`
 *   already returns every active source's channel, so multiple
 *   objects redirecting simultaneously falls out for free.
 *
 * SIMPLIFIED, documented scope cuts (not fabricated behaviour):
 * - @sendchannel/@sendchannel_sec implement only the BLANKET y/n form;
 *   the spec's own "specify a channel to except IT from the block"
 *   nuance (same command name, `:channel` option, `rem`/`add` instead
 *   of `y`/`n`) is not implemented - such a command is parsed but
 *   silently ignored (its param won't match "y"/"n"), same as any other
 *   unsupported variant elsewhere in this module. `_sec`'s object-
 *   scoping nuance (consistent with every prior category that has it)
 *   is also not implemented - it shares the plain command's key.
 * - Every `_sec` variant across this whole file follows that same
 *   precedent: shares its plain command's restriction key, no separate
 *   object-scoping logic.
 * - Blocked incoming IMs (`@recvim`) are silently dropped - the spec's
 *   "the sender is notified they cannot be read" half is not
 *   implemented, matching the same gap already accepted for
 *   Teleportation's `@tprequest`.
 *
 * `@sendim`'s "a bogus message is sent to the receiver instead" IS
 * implemented literally (the outgoing text is replaced, not the send
 * suppressed) - the spec is explicit about this and it cost no extra
 * mechanism.
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
#include "krlvchat.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "krlvhandler.h"

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

    // "@behaviour[:<UUID>]=<rem/add for exception>/<y/n for toggle>" shape
    // (recvchat/recvemote/sendim/startim/recvim) - no option toggles a
    // GLOBAL switch (keyed by imposing source); a UUID option toggles a
    // per-sender exception (keyed by the exempted UUID itself).
    void applyToggleOrException(const std::string& toggleKey, const std::string& exceptKey,
        const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        if (cmd.hasOption())
        {
            LLUUID target;
            if (target.set(cmd.option, false) && target.notNull())
            {
                if (cmd.param == "add") gKRlv.addRestriction(KRlvHandler::exceptionKey(exceptKey, target), sourceId);
                else if (cmd.param == "rem") gKRlv.removeRestriction(KRlvHandler::exceptionKey(exceptKey, target), sourceId);
            }
        }
        else
        {
            applyToggle(toggleKey, cmd, sourceId);
        }
    }

    // "@behaviourfrom:<UUID>=<y/n>" / "@behaviourto:<UUID>=<y/n>" shape
    // (recvchatfrom/recvemotefrom/sendimto/startimto/recvimfrom) - a
    // targeted block against ONE specific avatar, keyed by that avatar's
    // own UUID (not the imposing source).
    void applyPerTargetToggle(const std::string& key, const KRlvCommand& cmd)
    {
        if (!cmd.hasOption())
        {
            return;
        }
        LLUUID target;
        if (!target.set(cmd.option, false) || target.isNull())
        {
            return;
        }
        if (cmd.param == "n")
        {
            gKRlv.addRestriction(key, target, cmd.option);
        }
        else if (cmd.param == "y")
        {
            gKRlv.removeRestriction(key, target);
        }
    }

    // "@behaviour[:<option>]=<rem/add>" shape (emote/redirchat/
    // rediremote) - an exception-list toggle like applyToggleOrException()'s
    // UUID-option branch, but keyed by the imposing SOURCE (so multiple
    // objects can each add their own redirect channel independently,
    // matching the spec's "redirected to each channel" wording) rather
    // than by option value. `option` may legitimately be empty (@emote
    // has none at all).
    void applyAddRemToggle(const std::string& key, const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        if (cmd.param == "add")
        {
            gKRlv.addRestriction(key, sourceId, cmd.option);
        }
        else if (cmd.param == "rem")
        {
            gKRlv.removeRestriction(key, sourceId);
        }
    }
}

namespace KRlv
{
    EChatType getClampedChatType(EChatType original)
    {
        if (original != CHAT_TYPE_WHISPER && original != CHAT_TYPE_NORMAL && original != CHAT_TYPE_SHOUT)
        {
            return original;
        }

        // Ordinal scale: 0=whisper, 1=normal, 2=shout. @chatshout/
        // @chatnormal act as a CEILING, @chatwhisper as a FLOOR - see
        // krlvchat.h for the documented tie-break if they ever conflict.
        int ceiling = 2;
        int floor = 0;
        if (gKRlv.isRestricted("chatshout"))
        {
            ceiling = std::min(ceiling, 1);
        }
        if (gKRlv.isRestricted("chatnormal"))
        {
            ceiling = std::min(ceiling, 0);
        }
        if (gKRlv.isRestricted("chatwhisper"))
        {
            floor = std::max(floor, 1);
        }

        int level = (original == CHAT_TYPE_WHISPER) ? 0 : (original == CHAT_TYPE_NORMAL) ? 1 : 2;
        int clamped = (floor > ceiling) ? floor : std::max(floor, std::min(level, ceiling));

        if (clamped <= 0) return CHAT_TYPE_WHISPER;
        if (clamped >= 2) return CHAT_TYPE_SHOUT;
        return CHAT_TYPE_NORMAL;
    }

    bool isChannelSendBlocked(S32 channel)
    {
        std::string channelStr = std::to_string(channel);
        for (const std::string& option : gKRlv.getRestrictionOptions("sendchannel_except"))
        {
            if (option == channelStr)
            {
                return true;
            }
        }
        return false;
    }

    bool getChatRedirectChannels(bool isEmote, std::vector<S32>& outChannels)
    {
        outChannels.clear();
        const std::string& key = isEmote ? "rediremote" : "redirchat";
        for (const std::string& option : gKRlv.getRestrictionOptions(key))
        {
            outChannels.push_back(std::atoi(option.c_str()));
        }
        return !outChannels.empty();
    }

    bool filterSendChatText(std::string& text, bool isEmote)
    {
        // Special characters discard the WHOLE message outright,
        // regardless of @emote's exception state - spec: "the emotes
        // are not truncated anymore (however, special signs will still
        // discard the message)".
        static const std::string kSpecialChars = "()\"-*=_^";
        if (text.find_first_of(kSpecialChars) != std::string::npos)
        {
            return false;
        }

        // A period discards the REST of the message from that point on,
        // applied before the length truncation below - spec: "When a
        // period is present, the rest of the message is discarded."
        size_t period = text.find('.');
        if (period != std::string::npos)
        {
            text = text.substr(0, period);
        }

        // @emote is the documented exception to length truncation ONLY
        // - the special-char/period rules above still apply to it
        // regardless.
        if (isEmote && gKRlv.isRestricted("emote"))
        {
            return true;
        }

        const size_t maxLen = isEmote ? 30 : 15;
        if (text.length() > maxLen)
        {
            text = text.substr(0, maxLen);
        }
        return true;
    }
}

void krlv_register_chat_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("sendchat",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sendchat", cmd, sourceId); return true; });

    handler.registerBehaviour("chatshout",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("chatshout", cmd, sourceId); return true; });
    handler.registerBehaviour("chatnormal",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("chatnormal", cmd, sourceId); return true; });
    handler.registerBehaviour("chatwhisper",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("chatwhisper", cmd, sourceId); return true; });

    handler.registerBehaviour("recvchat",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggleOrException("recvchat", "recvchat_except", cmd, sourceId); return true; });
    handler.registerBehaviour("recvchat_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("recvchat", cmd, sourceId); return true; }); // shares "recvchat" key
    handler.registerBehaviour("recvchatfrom",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { applyPerTargetToggle("recvchatfrom", cmd); return true; });

    handler.registerBehaviour("sendgesture",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sendgesture", cmd, sourceId); return true; });

    handler.registerBehaviour("recvemote",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggleOrException("recvemote", "recvemote_except", cmd, sourceId); return true; });
    handler.registerBehaviour("recvemote_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("recvemote", cmd, sourceId); return true; }); // shares "recvemote" key
    handler.registerBehaviour("recvemotefrom",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { applyPerTargetToggle("recvemotefrom", cmd); return true; });

    handler.registerBehaviour("sendchannel",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sendchannel", cmd, sourceId); return true; }); // blanket form only - see file header
    handler.registerBehaviour("sendchannel_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sendchannel", cmd, sourceId); return true; }); // shares "sendchannel" key
    handler.registerBehaviour("sendchannel_except",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sendchannel_except", cmd, sourceId); return true; }); // option = blocked channel number

    handler.registerBehaviour("sendim",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggleOrException("sendim", "sendim_except", cmd, sourceId); return true; });
    handler.registerBehaviour("sendim_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sendim", cmd, sourceId); return true; }); // shares "sendim" key
    handler.registerBehaviour("sendimto",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { applyPerTargetToggle("sendimto", cmd); return true; });

    handler.registerBehaviour("startim",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggleOrException("startim", "startim_except", cmd, sourceId); return true; });
    handler.registerBehaviour("startimto",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { applyPerTargetToggle("startimto", cmd); return true; });

    handler.registerBehaviour("recvim",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggleOrException("recvim", "recvim_except", cmd, sourceId); return true; });
    handler.registerBehaviour("recvim_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("recvim", cmd, sourceId); return true; }); // shares "recvim" key
    handler.registerBehaviour("recvimfrom",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { applyPerTargetToggle("recvimfrom", cmd); return true; });

    handler.registerBehaviour("redirchat",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyAddRemToggle("redirchat", cmd, sourceId); return true; });
    handler.registerBehaviour("rediremote",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyAddRemToggle("rediremote", cmd, sourceId); return true; });
    handler.registerBehaviour("emote",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyAddRemToggle("emote", cmd, sourceId); return true; });
}
