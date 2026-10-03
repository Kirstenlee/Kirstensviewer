/**
 * @file krlvname.cpp
 * @brief KRLV "Name Tags and Hovertext" category - @shownames,
 * @shownames_sec, @shownametags, @shownearby, @showhovertextall,
 * @showhovertext, @showhovertexthud, @showhovertextworld.
 *
 * COMPLETE: all 8 command names have real handlers, though @shownearby
 * and the pie-menu-disclosure half of @shownames carry documented
 * simplifications below.
 *
 * @shownames[:except_uuid]=<y/n> uses the plain applyToggle() shape,
 * NOT an add/rem exception list - the syntax's "except_uuid" is simply
 * the command's own `option`, stored alongside the restriction the same
 * way every other optioned toggle already works. The exempted UUID is
 * read back via KRlvHandler::getRestrictionOptions("shownames") and
 * compared against the avatar in question at each query site.
 * @shownames_sec shares the exact same "shownames" restriction key -
 * the established, documented `_sec` simplification used by every
 * other `_sec` variant this session (same-imposing-object-only
 * exceptions are never modeled).
 *
 * @shownames and @shownametags are gated at TWO different, deliberately
 * chosen granularities:
 * - @shownames also censors chat/tooltips (KRlv::isNameCensored()) -
 *   the spec's own wording for it explicitly covers both.
 * - @shownametags does NOT touch chat - the spec's own wording says so
 *   ("won't censor the chat with dummy names").
 * - BOTH hide the floating nametag as a whole (KRlv::isNameTagHidden()),
 *   which on this viewer is the SAME LLHUDNameTag object that also
 *   carries a restricted avatar's floating chat BUBBLE (distinct from
 *   typed chat HISTORY) - so hiding the nametag also blanks their chat
 *   bubble. This is a deliberate, documented simplification (a more-
 *   restrictive-than-strictly-required side effect, the safe direction),
 *   not a bug - the alternative (gating only the identity-line text
 *   inside LLVOAvatar::idleUpdateNameTagText()) was investigated and
 *   rejected as unnecessary extra complexity for a cosmetic difference.
 *
 * Not implemented, documented rather than guessed at: the spec's "the
 * pie menu is almost useless so the user can't get the profile
 * directly" clause - no single chokepoint exists in this viewer's
 * context-menu code (the pie/context menu's own labels are static
 * strings, never the resolved avatar name), matching this session's
 * established precedent for chasing every UI surface (Teleportation's
 * undelivered decline notice, Chat's @redirchat/@rediremote/@emote).
 * A name learned via right-click still ultimately traces back to either
 * the (already-gated) nametag or tooltip, not the menu itself.
 *
 * @shownearby is PARTIAL: the minimap hover tooltip is gated (the ONLY
 * place the minimap itself exposes a name - confirmed via investigation,
 * dots only otherwise), but the People window's own "Nearby" tab list is
 * NOT - its name resolution (LLAvatarListItem::onAvatarNameCache()) is
 * shared, generic infrastructure used by the Friends/Recent/group-member
 * lists too, with no existing way to know "this row is inside the
 * Nearby tab specifically" at that shared callback. Gating it blindly
 * would incorrectly also censor Friends/Recent tab names, which
 * @shownearby is not supposed to touch - fixing this properly needs new
 * plumbing (a per-list context flag), out of scope for this pass.
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

#include "krlvname.h"
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

    // "@behaviour:<UUID>=<y/n>" shape (@showhovertext) - a targeted block
    // against ONE specific prim, keyed by that prim's own UUID (the spec
    // explicitly allows this to be issued by an object OTHER than the
    // target itself, unlike @detach's self-only shape).
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

    bool matchesExemptedUuid(const std::string& behaviour, const LLUUID& avatarId)
    {
        for (const std::string& opt : gKRlv.getRestrictionOptions(behaviour))
        {
            if (opt.empty())
            {
                continue;
            }
            LLUUID exempted;
            if (exempted.set(opt, false) && exempted == avatarId)
            {
                return true;
            }
        }
        return false;
    }
}

namespace KRlv
{
    bool isNameCensored(const LLUUID& avatarId)
    {
        if (!gKRlv.isRestricted("shownames"))
        {
            return false;
        }
        return !matchesExemptedUuid("shownames", avatarId);
    }

    bool isNameTagHidden(const LLUUID& avatarId)
    {
        if (isNameCensored(avatarId))
        {
            return true;
        }
        return gKRlv.isRestricted("shownametags");
    }

    bool isNearbyNameHidden()
    {
        return gKRlv.isRestricted("shownearby");
    }

    bool isHovertextHidden(const LLUUID& sourceObjectId, bool isHudAttachment)
    {
        if (gKRlv.isRestricted("showhovertextall"))
        {
            return true;
        }
        if (isHudAttachment && gKRlv.isRestricted("showhovertexthud"))
        {
            return true;
        }
        if (!isHudAttachment && gKRlv.isRestricted("showhovertextworld"))
        {
            return true;
        }
        return sourceObjectId.notNull() && gKRlv.hasRestrictionFrom("showhovertext", sourceObjectId);
    }
}

void krlv_register_name_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("shownames",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("shownames", cmd, sourceId); return true; });

    handler.registerBehaviour("shownames_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("shownames", cmd, sourceId); return true; });

    handler.registerBehaviour("shownametags",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("shownametags", cmd, sourceId); return true; });

    handler.registerBehaviour("shownearby",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("shownearby", cmd, sourceId); return true; });

    handler.registerBehaviour("showhovertextall",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("showhovertextall", cmd, sourceId); return true; });

    handler.registerBehaviour("showhovertext",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { applyPerTargetToggle("showhovertext", cmd); return true; });

    handler.registerBehaviour("showhovertexthud",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("showhovertexthud", cmd, sourceId); return true; });

    handler.registerBehaviour("showhovertextworld",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("showhovertextworld", cmd, sourceId); return true; });
}
