/**
 * @file krlvmisc.cpp
 * @brief KRLV "Miscellaneous" category - @notify, @permissive, @clear,
 * @getstatus, @getstatusall.
 *
 * COMPLETE: all 5 command names have real handlers, though @permissive
 * carries a significant documented limitation - see below.
 *
 * @notify:<channel>[;word]=<rem/add> is stored as an ordinary restriction
 * entry (key "notify", option = "channel;word" verbatim) - no new
 * storage needed. The actual reporting logic lives in
 * KRlvHandler::notifyRestrictionChange() (krlvhandler.h/.cpp), called
 * from WITHIN addRestriction()/removeRestriction()/
 * clearRestrictionsFrom() themselves, so every category's restriction
 * changes are reported automatically with zero per-category
 * involvement. Reports SYSTEM-WIDE (every restriction change from any
 * object, not just the subscribing one) - the spec's own text ("repeat
 * any restriction it adds or removes") reads as the VIEWER doing the
 * repeating, not scoped to the subscriber's own impositions, and a
 * scope-limited reading would need tracking "which object does this
 * exception's target belong to" info this codebase doesn't keep (see
 * @clear's own limitation below for why). `@clear`-triggered removals
 * correctly omit the "=y" suffix per the spec's own explicit "@clear
 * will not add an equal sign" rule (KRlvHandler::NotifyKind::Cleared).
 *
 * @clear (bare) / @clear=<string> - real implementation via
 * KRlvHandler::clearRestrictionsFrom(), a full scan across every
 * restriction key removing every entry whose entry.sourceObjectId
 * matches the issuing object. Real, significant, HONESTLY DOCUMENTED
 * limitation: many restrictions across already-shipped categories key
 * their entries by a TARGET id rather than the imposing object's own id
 * (@editobj:<UUID>=n, @touchthis:<UUID>=rem/add, @showhovertext:<UUID>=n,
 * Shared Folders' per-folder restrictions, Attachments' per-point locks,
 * ...) - from clearRestrictionsFrom()'s point of view there is no way to
 * tell "this entry's source field holds a TARGET" apart from "this
 * entry's source field holds the IMPOSING object", so @clear sent by the
 * object that imposed e.g. `@editobj:<some_uuid>=n` will NOT clear it
 * (the entry is keyed by <some_uuid>, not by the imposing object's own
 * id). Retrofitting every per-target restriction shape across 8+ already
 * -shipped categories to separately track "who imposed this" alongside
 * "what does this target" would be a large, cross-category refactor -
 * explicitly out of scope for this pass, flagged here rather than
 * silently shipping a @clear that looks complete but quietly misses a
 * real, non-trivial slice of restriction shapes. @getstatus/
 * @getstatusall share this exact same limitation (see
 * KRlvHandler::getActiveRules()'s own comment).
 *
 * @permissive=<y/n> is a REAL TOGGLE (so @getstatus/@clear see it, so a
 * script checking its own state gets an accurate answer) but has NO
 * FUNCTIONAL EFFECT - and this is a deliberate, honest choice, not an
 * oversight. Its entire purpose is to force every "_sec"-suffixed
 * command's exception handling to become same-object-only even when the
 * PLAIN (non-"_sec") form of that restriction is active. But every
 * "_sec" variant shipped this session (Teleportation's @tplure_sec,
 * Attachments' @shownames_sec, Chat's @sendchat family, ...) was already
 * documented, repeatedly, as sharing its plain counterpart's restriction
 * key outright - the same-object-only nuance was never built for ANY of
 * them. @permissive therefore has nothing real to toggle: implementing
 * it honestly would mean retrofitting that nuance into every "_sec"
 * command across every category first, a change with a footprint far
 * larger than this one category's own reasonable scope. Landed as
 * inert-but-present rather than silently pretended-functional.
 *
 * @getstatus[:filter[;sep]]/@getstatusall[:filter[;sep]] both reply with
 * a `sep`-joined (default "/") list of "behaviour[:option]" pairs, with
 * a leading `sep` per the spec's own explicit "a slash is prepended"
 * wording - @getstatus scoped to the issuing object's own entries (via
 * KRlvHandler::getActiveRules(sourceId)), @getstatusall to every active
 * entry regardless of source (LLUUID::null passed as the wildcard).
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

    // @getstatus/@getstatusall share everything except which source(s)
    // getActiveRules() looks at.
    std::string buildStatusReply(const LLUUID& sourceId, const std::string& option)
    {
        std::string filter = option;
        std::string sep = "/";
        const size_t semi = option.find(';');
        if (semi != std::string::npos)
        {
            filter = option.substr(0, semi);
            sep = option.substr(semi + 1);
        }

        std::string reply = sep; // spec: "a slash is prepended at the beginning"
        bool first = true;
        for (const auto& [behaviour, ruleOption] : gKRlv.getActiveRules(sourceId))
        {
            if (!filter.empty() && behaviour.find(filter) == std::string::npos)
            {
                continue;
            }
            if (!first)
            {
                reply += sep;
            }
            reply += behaviour;
            if (!ruleOption.empty())
            {
                reply += ":" + ruleOption;
            }
            first = false;
        }
        return reply;
    }
}

void krlv_register_misc_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("notify",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            // One key per subscription ("notify:<channel;word>"), owned by the object, so
            // several subscriptions from one object coexist and rem removes only its own.
            const std::string key = "notify:" + cmd.option;
            if (cmd.param == "add")
            {
                gKRlv.addRestriction(key, sourceId);
            }
            else if (cmd.param == "rem")
            {
                gKRlv.removeRestriction(key, sourceId);
            }
            return true;
        });

    handler.registerBehaviour("permissive",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("permissive", cmd, sourceId); return true; });

    handler.registerBehaviour("clear",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            // "@clear" (bare) has cmd.param empty; "@clear=<string>" has
            // it set to the filter substring - both handled identically.
            gKRlv.clearRestrictionsFrom(cmd.param, sourceId);
            return true;
        });

    handler.registerBehaviour("getstatus",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            replyOnChannel(buildStatusReply(sourceId, cmd.option), cmd.param);
            return true;
        });

    handler.registerBehaviour("getstatusall",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(buildStatusReply(LLUUID::null, cmd.option), cmd.param);
            return true;
        });
}
