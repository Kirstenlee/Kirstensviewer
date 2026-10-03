/**
 * @file krlvattachment.cpp
 * @brief KRLV "Clothing and Attachments" category - @detach, @addattach,
 * @remattach, @defaultwear, @addoutfit, @remoutfit, @getoutfit, @getattach,
 * @acceptpermission, @denypermission, @detachme.
 *
 * COMPLETE: all 11 command names have real handlers, though several carry
 * documented simplifications below.
 *
 * Point/part locking scheme: each named attachment point or clothing-layer
 * "part" gets its OWN dynamically-keyed restriction ("attach_point:torso",
 * "addoutfit:shirt", ...), with a separate "..._all" key for the no-point/
 * no-part ("every point"/"every layer") form every one of these commands
 * accepts. This is deliberate, not incidental: KRlvHandler's restriction
 * table dedupes on (behaviour key, source id) alone, so a single source
 * object locking several DIFFERENT points/parts at once (e.g. two
 * successive "@addattach:spine=n" / "@addattach:chest=n" from the same
 * script) needs each point to live under its own key - reusing one shared
 * "addattach" key with the point name only in the option string would
 * silently drop every point after the first from the same source. See
 * krlv/README.md's Attachments section.
 *
 * "@detach:<point>=<y/n>" locks BOTH attaching to and detaching from that
 * point (writes both the detach_point and attach_point keys) - the spec's
 * own text for this one command already describes exactly that ("locked
 * either full or empty... no item will be able to be attached or detached
 * there"), unlike @addattach/@remattach which are one-directional by
 * design. @remattach[:<point>]=<y/n> is read as "point locked full" in the
 * detach-blocking sense only (whatever occupies the point, present or
 * future, becomes undetachable) - it does NOT also block new attachment,
 * since the spec's own wording for it is internally inconsistent (a known
 * gap in the scraped dataset this project draws from - see
 * krlv/README.md), and the safer, name-matching reading was chosen instead
 * of guessing at the missing half.
 *
 * @detach[:attachpt]=force / @remattach[...]=force (an alias) /
 * @detachme=force all share ONE force-detach hook (KRlvForceDetachRequest):
 * an empty point name means "detach the sending object itself", a
 * non-empty one is tried first against a real attachment-point name, then
 * against a wearable-type name (so "@remoutfit:shirt=force" can reuse the
 * exact same hook) - see llappviewer.cpp's KRLV_TOUCHPOINT block.
 * "@detach:<folder_name>=force" (Shared-Folders-addressed detach) and bare
 * "@remoutfit=force" with no part (strip every clothing layer at once) are
 * both NOT implemented - the former needs the #RLV inventory structure
 * this project's Shared Folders category (not yet started) owns, the
 * latter's blast radius (every worn clothing item at once) was judged not
 * worth the risk of a mis-scoped first implementation; both fail safe by
 * simply doing nothing.
 *
 * @defaultwear=<y/n> is the one command in this file whose y/n polarity is
 * INVERTED from every other KRLV toggle: "y" is the opt-in permissive
 * state (the spec's own wording - "when allowed[y]..."), not the usual
 * "remove a restriction" meaning, so it is handled with its own small
 * block rather than the shared applyToggle() helper, and read back through
 * a dedicated isDefaultWearAllowed() query rather than isRestricted().
 *
 * @getoutfit[:part] / @getattach[:attachpt] both need a LIVE read of
 * current worn-item/attachment-point state (LLWearableType/
 * LLViewerJointAttachment enumeration), so they route through their own
 * hooks (KRlvGetOutfitLayersRequest/KRlvGetAttachPointsRequest) rather than
 * echoing a stored restriction value - see krlvattachment.h.
 *
 * @acceptpermission/@denypermission gate LLNotificationsUtil::add() at its
 * one call site in process_script_question() (llviewermessage.cpp),
 * force-responding via the same LLNotifications::forceResponse() mechanism
 * llappearancemgr.cpp's rez_attachment() already uses for its own
 * confirmation dialog. @denypermission wins if both are somehow active
 * (the spec's own stated precedence). @denypermission is marked DEPRECATED
 * in the spec's own text; it is still implemented here since KRLV
 * implements the full command surface regardless of upstream deprecation
 * notices.
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

#include "krlvattachment.h"
#include "krlvhandler.h"

// KRLV: same plain external-linkage free function krlvversion.cpp/
// krlvsitting.cpp forward-declare locally.
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

    // See file header - each named point/part gets its own key, with a
    // separate "..._all" key for the no-option ("every point"/"every
    // layer") form.
    std::string pointKey(const std::string& prefix, const std::string& name)
    {
        return name.empty() ? (prefix + "_all") : (prefix + ":" + name);
    }

    void applyPointToggle(const std::string& prefix, const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        const std::string key = pointKey(prefix, cmd.option);
        if (cmd.param == "n")
        {
            gKRlv.addRestriction(key, sourceId);
        }
        else if (cmd.param == "y")
        {
            gKRlv.removeRestriction(key, sourceId);
        }
    }

    void applyDetachPointToggle(const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        const std::string detachKey = pointKey("detach_point", cmd.option);
        const std::string attachKey = pointKey("attach_point", cmd.option);
        if (cmd.param == "n")
        {
            gKRlv.addRestriction(detachKey, sourceId);
            gKRlv.addRestriction(attachKey, sourceId);
        }
        else if (cmd.param == "y")
        {
            gKRlv.removeRestriction(detachKey, sourceId);
            gKRlv.removeRestriction(attachKey, sourceId);
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

namespace
{
    bool checkPointLock(const std::string& prefix, const std::string& name)
    {
        if (gKRlv.isRestricted(prefix + "_all"))
        {
            return true;
        }
        return !name.empty() && gKRlv.isRestricted(prefix + ":" + name);
    }
}

namespace KRlv
{
    bool isObjectDetachLocked(const LLUUID& objectId)
    {
        return gKRlv.hasRestrictionFrom("detach", objectId);
    }

    bool isDetachPointLocked(const std::string& pointName)
    {
        return checkPointLock("detach_point", pointName);
    }

    bool isAttachPointLocked(const std::string& pointName)
    {
        return checkPointLock("attach_point", pointName);
    }

    bool isAnySpecificAttachPointLocked()
    {
        return gKRlv.hasAnyRestrictionWithPrefix("attach_point:");
    }

    bool isOutfitPartAttachLocked(const std::string& partName)
    {
        return checkPointLock("addoutfit", partName);
    }

    bool isOutfitPartDetachLocked(const std::string& partName)
    {
        return checkPointLock("remoutfit", partName);
    }

    bool isDefaultWearAllowed()
    {
        return gKRlv.isRestricted("defaultwear_bypass");
    }

    ScriptPermissionResponse getScriptPermissionAutoResponse()
    {
        if (gKRlv.isRestricted("denypermission"))
        {
            return ScriptPermissionResponse::AutoDeny;
        }
        if (gKRlv.isRestricted("acceptpermission"))
        {
            return ScriptPermissionResponse::AutoAccept;
        }
        return ScriptPermissionResponse::None;
    }
}

void krlv_register_attachment_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("detach",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                // "@detach=force" / "@detach:<attachpt>=force" - empty
                // option means self-detach (see file header).
                gKRlv.requestForceDetach(sourceId, cmd.option);
            }
            else if (cmd.hasOption())
            {
                // "@detach:<attach_point_name>=<y/n>" - locks both
                // directions at that point.
                applyDetachPointToggle(cmd, sourceId);
            }
            else
            {
                // "@detach=<y/n>" - plain self-restriction.
                applyToggle("detach", cmd, sourceId);
            }
            return true;
        });

    handler.registerBehaviour("addattach",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyPointToggle("attach_point", cmd, sourceId);
            return true;
        });

    handler.registerBehaviour("remattach",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                // Alias of "@detach[:attachpt]=force" (see file header).
                gKRlv.requestForceDetach(sourceId, cmd.option);
            }
            else
            {
                applyPointToggle("detach_point", cmd, sourceId);
            }
            return true;
        });

    handler.registerBehaviour("defaultwear",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            // Inverted polarity - see file header.
            if (cmd.param == "y")
            {
                gKRlv.addRestriction("defaultwear_bypass", sourceId);
            }
            else if (cmd.param == "n")
            {
                gKRlv.removeRestriction("defaultwear_bypass", sourceId);
            }
            return true;
        });

    handler.registerBehaviour("addoutfit",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyPointToggle("addoutfit", cmd, sourceId);
            return true;
        });

    handler.registerBehaviour("remoutfit",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                // Bare "@remoutfit=force" (no part) is deferred - see
                // file header.
                if (cmd.hasOption())
                {
                    gKRlv.requestForceDetach(sourceId, cmd.option);
                }
            }
            else
            {
                applyPointToggle("remoutfit", cmd, sourceId);
            }
            return true;
        });

    handler.registerBehaviour("getoutfit",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(gKRlv.requestGetOutfitLayers(cmd.option), cmd.param);
            return true;
        });

    handler.registerBehaviour("getattach",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(gKRlv.requestGetAttachPoints(cmd.option), cmd.param);
            return true;
        });

    handler.registerBehaviour("acceptpermission",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "add")
            {
                gKRlv.addRestriction("acceptpermission", sourceId);
            }
            else if (cmd.param == "rem")
            {
                gKRlv.removeRestriction("acceptpermission", sourceId);
            }
            return true;
        });

    handler.registerBehaviour("denypermission",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "add")
            {
                gKRlv.addRestriction("denypermission", sourceId);
            }
            else if (cmd.param == "rem")
            {
                gKRlv.removeRestriction("denypermission", sourceId);
            }
            return true;
        });

    handler.registerBehaviour("detachme",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                gKRlv.requestForceDetach(sourceId, std::string());
            }
            return true;
        });
}
