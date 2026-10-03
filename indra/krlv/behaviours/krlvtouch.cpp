/**
 * @file krlvtouch.cpp
 * @brief KRLV "Touch" category - @fartouch, @touchfar, @touchall,
 * @touchworld, @touchthis, @touchme, @touchattach, @touchattachself,
 * @touchattachother, @touchhud, @interact.
 *
 * COMPLETE: all 11 command names have real handlers, no stubs.
 *
 * Every real chokepoint this category needs is ONE function,
 * send_ObjectGrab_message() (lltoolgrab.cpp) - confirmed via
 * investigation to be the single low-level ObjectGrab-message sender
 * every touch/grab UI path already converges into: the direct left-
 * click grab tool (lltoolgrab.cpp's own drag-grab handler), the pie-menu
 * "Touch" action (handle_object_touch(), llviewermenu.cpp), and the
 * LSL-bridge agent-touch API (llagentlistener.cpp) all call it. One gate
 * covers all three UI/API surfaces.
 *
 * @touchhud[:<UUID>] and @touchattachother[:<UUID>] share ONE shape:
 * the optional UUID NARROWS the restriction's scope (empty = "every
 * HUD"/"every other avatar", a specific id = just that one), not an
 * exception - read back by scanning every active source's option string
 * via KRlvHandler::getRestrictionOptions(), matching an empty option OR
 * an exact id match. @touchworld[:<UUID>] is different despite the
 * similar syntax - its own spec text explicitly calls the UUID form "an
 * exception", so it uses the established applyToggleOrException() shape
 * instead (a SEPARATE "touchworld_except" key, add/rem verbs). @touchthis
 * is different again - a direct per-target block/unblock using add/rem
 * as its OWN toggle verbs (not an exception to anything), keyed by the
 * target object's own id. @touchme is a per-SOURCE exemption (the
 * imposing object marking itself as always-touchable regardless of any
 * other active touch restriction) - checked first, before anything else,
 * at the shared chokepoint.
 *
 * @interact is the one cross-cutting command in this category - the
 * spec states it also blocks editing, rezzing and sitting, categories
 * this session already shipped with their own restriction keys.
 * KRlv::isInteractBlocked() is checked as one more OR-condition at
 * those categories' own already-gated chokepoints (handle_object_edit(),
 * handle_object_delete()/dropObject(), handle_object_sit()) rather than
 * duplicating their logic here - see krlv/README.md.
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
#include "lluuid.h"

#include "krlvtouch.h"
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

    void applyToggleOrException(const std::string& toggleKey, const std::string& exceptKey,
                                 const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        if (cmd.hasOption())
        {
            LLUUID target;
            if (target.set(cmd.option, false) && target.notNull())
            {
                if (cmd.param == "add")
                {
                    gKRlv.addRestriction(KRlvHandler::exceptionKey(exceptKey, target), sourceId);
                }
                else if (cmd.param == "rem")
                {
                    gKRlv.removeRestriction(KRlvHandler::exceptionKey(exceptKey, target), sourceId);
                }
            }
        }
        else
        {
            applyToggle(toggleKey, cmd, sourceId);
        }
    }

    // "@touchthis:<UUID>=<rem/add>" - a direct per-target block, using
    // add/rem as its OWN toggle verbs rather than exempting from another
    // restriction.
    void applyPerTargetToggleAddRem(const std::string& key, const KRlvCommand& cmd)
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
        if (cmd.param == "add")
        {
            gKRlv.addRestriction(key, target, cmd.option);
        }
        else if (cmd.param == "rem")
        {
            gKRlv.removeRestriction(key, target);
        }
    }

    // @touchhud[:<UUID>]/@touchattachother[:<UUID>] - empty option on
    // ANY active entry means "every target"; otherwise match the exact
    // scoped id.
    bool isOptionScopedLock(const std::string& key, const LLUUID& targetId)
    {
        for (const std::string& opt : gKRlv.getRestrictionOptions(key))
        {
            if (opt.empty())
            {
                return true;
            }
            LLUUID scoped;
            if (scoped.set(opt, false) && scoped == targetId)
            {
                return true;
            }
        }
        return false;
    }
}

namespace KRlv
{
    bool isTouchBlocked(const LLUUID& objectId, bool isAttachment, bool isHud,
                         bool isSelfAttachment, const LLUUID& attachedAvatarId)
    {
        // "the user can touch this object in particular" - always wins.
        if (gKRlv.hasRestrictionFrom("touchme", objectId))
        {
            return false;
        }
        if (gKRlv.isRestricted("interact"))
        {
            return true;
        }
        if (gKRlv.hasRestrictionFrom("touchthis", objectId))
        {
            return true;
        }

        if (isHud)
        {
            return isOptionScopedLock("touchhud", objectId);
        }

        if (isAttachment)
        {
            if (gKRlv.isRestricted("touchall") || gKRlv.isRestricted("touchattach"))
            {
                return true;
            }
            if (isSelfAttachment)
            {
                return gKRlv.isRestricted("touchattachself");
            }
            return isOptionScopedLock("touchattachother", attachedAvatarId);
        }

        // In-world (non-attachment, non-HUD) object.
        if (gKRlv.isRestricted("touchall"))
        {
            return true;
        }
        return gKRlv.isRestricted("touchworld") && !gKRlv.hasRestrictionFrom("touchworld_except", objectId);
    }

    bool getTouchDistanceLimit(F32& outMaxDist)
    {
        bool restricted = false;
        F32 minDist = 0.f;
        for (const std::string& opt : gKRlv.getRestrictionOptions("fartouch"))
        {
            const F32 dist = opt.empty() ? 1.5f : static_cast<F32>(std::atof(opt.c_str()));
            if (!restricted || dist < minDist)
            {
                minDist = dist;
            }
            restricted = true;
        }
        if (restricted)
        {
            outMaxDist = minDist;
        }
        return restricted;
    }

    bool isInteractBlocked()
    {
        return gKRlv.isRestricted("interact");
    }
}

void krlv_register_touch_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("fartouch",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("fartouch", cmd, sourceId); return true; });
    handler.registerBehaviour("touchfar",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("fartouch", cmd, sourceId); return true; }); // synonym of @fartouch, per spec

    handler.registerBehaviour("touchall",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("touchall", cmd, sourceId); return true; });

    handler.registerBehaviour("touchworld",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggleOrException("touchworld", "touchworld_except", cmd, sourceId); return true; });

    handler.registerBehaviour("touchthis",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { applyPerTargetToggleAddRem("touchthis", cmd); return true; });

    handler.registerBehaviour("touchme",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "add")
            {
                gKRlv.addRestriction("touchme", sourceId);
            }
            else if (cmd.param == "rem")
            {
                gKRlv.removeRestriction("touchme", sourceId);
            }
            return true;
        });

    handler.registerBehaviour("touchattach",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("touchattach", cmd, sourceId); return true; });
    handler.registerBehaviour("touchattachself",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("touchattachself", cmd, sourceId); return true; });
    handler.registerBehaviour("touchattachother",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("touchattachother", cmd, sourceId); return true; });

    handler.registerBehaviour("touchhud",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("touchhud", cmd, sourceId); return true; });

    handler.registerBehaviour("interact",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("interact", cmd, sourceId); return true; });
}
