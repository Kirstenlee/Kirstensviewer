/**
 * @file krlvinventory.cpp
 * @brief KRLV "Inventory, Editing and Rezzing" category.
 *
 * COMPLETE: all 11 command names - the cleanest category tackled so far,
 * confirmed via investigation before writing any code: every command
 * reduces to a gate on an existing function or object property, no
 * reroutes, no new string-processing, no rendering-pipeline work.
 *
 * @showinv/@viewnote/@viewscript/@viewtexture reuse the existing
 * `registerFloaterGate()` mechanism from Location/Camera - zero new
 * touchpoints needed for any of the 4, on top of the one floater-close
 * hook already registered in llappviewer.cpp.
 *
 * @edit/@edit:<UUID>/@editobj:<UUID>/@editworld/@editattach all gate the
 * SAME single real chokepoint, `handle_object_edit()` (llviewermenu.cpp),
 * using the object already held in `LLSelectMgr`'s current selection at
 * that point (`LLViewerObject::isAttachment()`/`isHUDAttachment()`
 * distinguish in-world objects from worn attachments from HUDs). This
 * only gates ENTERING edit mode for a target - matches the spec's own
 * literal "the window will refuse to open" wording; switching selection
 * while the Build floater is already open doesn't re-trigger it.
 *
 * @rez gates 2 real functions (not 1): `LLToolDragAndDrop::dropObject()`
 * (rez-from-inventory and attachment-dropping - itself already a single
 * chokepoint several UI paths converge into) and `handle_object_delete()`
 * (deletion). Object creation from the build-tool palette (building a
 * prim from scratch, not from inventory) was not confirmed to route
 * through either of these - if it turns out to use a separate path, it
 * is NOT currently gated by @rez; flagged here rather than assumed
 * covered.
 *
 * @share/@share_sec/@share:<UUID> gate `LLGiveInventory::
 * doGiveInventoryItem()`/`doGiveInventoryCategory()` (llgiveinventory.cpp)
 * - both already take the recipient's UUID as their first parameter, so
 * the `@share:<UUID>` exception needs no extra plumbing to find it.
 *
 * `_sec` follows the same precedent as every prior category with it:
 * shares its plain command's restriction key, the object-scoping nuance
 * is not implemented.
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
    // (edit/share) - no option toggles a GLOBAL switch (keyed by the
    // imposing source); a UUID option toggles a per-target exception
    // (keyed by the exempted UUID itself).
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

    // "@behaviour:<UUID>=<y/n>" shape (editobj) - a targeted block
    // against ONE specific object, keyed by that object's own UUID.
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
}

void krlv_register_inventory_commands(KRlvHandler& handler)
{
    handler.registerFloaterGate("showinv", "inventory");
    handler.registerBehaviour("showinv",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("showinv", cmd, sourceId); return true; });

    handler.registerFloaterGate("viewnote", "preview_notecard");
    handler.registerBehaviour("viewnote",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("viewnote", cmd, sourceId); return true; });

    handler.registerFloaterGate("viewscript", "preview_script");
    handler.registerBehaviour("viewscript",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("viewscript", cmd, sourceId); return true; });

    handler.registerFloaterGate("viewtexture", "preview_texture");
    handler.registerBehaviour("viewtexture",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("viewtexture", cmd, sourceId); return true; });

    handler.registerBehaviour("edit",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggleOrException("edit", "edit_except", cmd, sourceId); return true; });
    handler.registerBehaviour("editobj",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { applyPerTargetToggle("editobj", cmd); return true; });
    handler.registerBehaviour("editworld",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("editworld", cmd, sourceId); return true; });
    handler.registerBehaviour("editattach",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("editattach", cmd, sourceId); return true; });

    handler.registerBehaviour("rez",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("rez", cmd, sourceId); return true; });

    handler.registerBehaviour("share",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggleOrException("share", "share_except", cmd, sourceId); return true; });
    handler.registerBehaviour("share_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("share", cmd, sourceId); return true; }); // shares "share" key
}
