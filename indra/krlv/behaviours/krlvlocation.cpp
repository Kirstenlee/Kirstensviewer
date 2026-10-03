/**
 * @file krlvlocation.cpp
 * @brief KRLV "Location" category - @showworldmap, @showminimap, @showloc.
 *
 * @showworldmap and @showminimap are COMPLETE: registerFloaterGate() below
 * ties each to its floater, and KRlvHandler::idle() (krlvhandler.cpp)
 * reactively closes it every frame it's restricted AND actually visible -
 * see krlvhandler.h's KRlvFloaterCloseRequest for why this is a poll
 * rather than a gate at each possible UI entry point (a toolbar button
 * was found to bypass an earlier onOpen()-based attempt at this; polling
 * catches every entry point, existing or future, with zero extra
 * touchpoints).
 *
 * @showloc is a PARTIAL implementation - see krlv/README.md. The real
 * spec also blocks landmark creation, blocks buying land, hides the About
 * Land box, hides the teleport-arrival parcel message, and obfuscates
 * region/parcel names inside system/object chat text (with an explicit
 * llOwnerSay exception so radars still work). Only two pieces are wired
 * up in this pass: hiding the top-menubar parcel/region text
 * (llpaneltopinfobar.cpp's setParcelInfoText()) and implying
 * @showworldmap (gates the same "world_map" floater).
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
    // KRLV: RLVa's y/n is not a live toggle value - "=n" ADDS the
    // restriction from this source, "=y" RELEASES that same source's
    // hold on it. Two different objects can each hold the same
    // restriction independently (see krlvrestriction.h) - only a
    // source's OWN "=y" releases its OWN hold. Purely bookkeeping - the
    // idle-tick floater gate (registerFloaterGate(), below) is what
    // actually reacts to the restriction, on whatever cadence it polls
    // at, so this never has to reach for LLFloaterReg itself.
    void applyToggle(const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        if (cmd.param == "n")
        {
            gKRlv.addRestriction(cmd.behaviour, sourceId, cmd.option);
        }
        else if (cmd.param == "y")
        {
            gKRlv.removeRestriction(cmd.behaviour, sourceId);
        }
        // Anything else (malformed param) is silently ignored, same as
        // every other KRLV command - RLVa's own "ignore what you don't
        // understand" convention.
    }
}

void krlv_register_location_commands(KRlvHandler& handler)
{
    handler.registerFloaterGate("showworldmap", "world_map");
    handler.registerFloaterGate("showminimap", "mini_map");
    // @showloc implies @showworldmap's "the world map is hidden" wording -
    // gates the same floater, independently of @showworldmap's own hold.
    handler.registerFloaterGate("showloc", "world_map");

    handler.registerBehaviour("showworldmap",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyToggle(cmd, sourceId);
            return true;
        });

    handler.registerBehaviour("showminimap",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyToggle(cmd, sourceId);
            return true;
        });

    handler.registerBehaviour("showloc",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            // See the file header for what @showloc does NOT cover yet.
            applyToggle(cmd, sourceId);
            return true;
        });
}
