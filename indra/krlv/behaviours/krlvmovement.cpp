/**
 * @file krlvmovement.cpp
 * @brief KRLV "Movement" category - @fly, @temprun, @alwaysrun, @setrot.
 *
 * @fly, @temprun and @alwaysrun are COMPLETE: each is a single real
 * chokepoint in newview (LLAgent::canFly(), agent_handle_doubletap_run(),
 * LLAgent::setAlwaysRun() respectively - see krlv/README.md's touch
 * points table), reached via a plain `gKRlv.isRestricted(...)` query from
 * newview code that already links krlv/ - no hook needed for that
 * direction.
 *
 * @setrot is COMPLETE as a one-shot force action: unlike a restriction,
 * it commands the avatar to rotate immediately. Since krlv/ cannot reach
 * LLAgent directly (llagent.h is newview-only), this uses the same
 * hook indirection as the floater-close mechanism - see
 * KRlvForceRotateRequest in krlvhandler.h.
 *
 * @adjustheight is COMPLETE as a one-shot force action, via
 * KRlvForceAdjustHeightRequest (krlvhandler.h) - re-investigated after
 * initially being left as a stub (the spec's prose never states the
 * formula relating distance_pelvis_to_foot/factor/delta to the resulting
 * offset). Found that this viewer already has a real, first-class,
 * user-facing "Hover Height" feature (`LLFloaterHoverHeight`,
 * `gSavedPerAccountSettings` "AvatarHoverOffsetZ",
 * `LLVOAvatarSelf::setHoverOffset()`) plus a real, live, always-current
 * avatar measurement (`LLAvatarAppearance::getPelvisToFoot()`) - and the
 * command's own first parameter name, "distance_pelvis_to_foot_in_meters",
 * is exactly that existing SL-platform measurement concept, not proprietary
 * RLVa internals. The formula implemented (in the hook, llappviewer.cpp) is
 * a DERIVED, not confirmed-from-spec, reading of what the parameter names
 * themselves describe:
 *
 *   hover_delta = (targetPelvisToFoot - getPelvisToFoot()) * factor + delta
 *   new_offset  = clamp(current_AvatarHoverOffsetZ + hover_delta,
 *                        MIN_HOVER_Z, MAX_HOVER_Z)
 *
 * Grounded in real, existing platform APIs (not fabricated), but NOT
 * verified against RLVa's own internal math (never read, per the
 * clean-room constraint) - same class of engineering call as @setrot's
 * north=+Y convention. User's own call: "some implementation is more
 * critical than a stub" - if real-world usage shows this formula is
 * wrong, it can be corrected later; shipping a reasoned best guess beats
 * leaving a known-real command entirely unimplemented.
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

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "krlvhandler.h"

namespace
{
    // Same "=n" adds / "=y" releases (per-source) semantics as
    // krlvlocation.cpp's applyToggle() - duplicated locally rather than
    // shared, consistent with this project's one-file-per-category style.
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
    }
}

void krlv_register_movement_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("fly",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyToggle(cmd, sourceId);
            return true;
        });

    handler.registerBehaviour("temprun",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyToggle(cmd, sourceId);
            return true;
        });

    handler.registerBehaviour("alwaysrun",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyToggle(cmd, sourceId);
            return true;
        });

    handler.registerBehaviour("setrot",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            // "@setrot:<angle_in_radians>=force" - angle is the option,
            // not the param (the param is the literal word "force").
            if (!cmd.hasOption())
            {
                return true;
            }
            const F32 angle = static_cast<F32>(std::atof(cmd.option.c_str()));

            // SL world convention: north is +Y, east is +X - a bearing
            // measured clockwise from north (as the spec describes)
            // converts to a horizontal direction vector as (sin, cos, 0).
            // This is standard Second Life platform geometry, not
            // anything specific to RLV/RLVa.
            LLVector3 lookAt(std::sin(angle), std::cos(angle), 0.f);
            lookAt.normalize();
            gKRlv.requestForceRotate(lookAt);
            return true;
        });

    handler.registerBehaviour("adjustheight",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            // "@adjustheight:<distance_pelvis_to_foot>;<factor>[;delta]=force"
            // - option carries all 2-3 numeric sub-values, semicolon-
            // separated (same convention @camdrawcolor's own RGB option
            // already uses in krlvcamera.cpp). delta defaults to 0 if the
            // spec's optional 3rd value was omitted.
            if (!cmd.hasOption())
            {
                return true;
            }

            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= cmd.option.size())
            {
                size_t semi = cmd.option.find(';', start);
                if (semi == std::string::npos)
                {
                    parts.push_back(cmd.option.substr(start));
                    break;
                }
                parts.push_back(cmd.option.substr(start, semi - start));
                start = semi + 1;
            }
            if (parts.size() < 2 || parts.size() > 3)
            {
                return true;
            }

            const F32 targetPelvisToFoot = static_cast<F32>(std::atof(parts[0].c_str()));
            const F32 factor = static_cast<F32>(std::atof(parts[1].c_str()));
            const F32 delta = (parts.size() == 3) ? static_cast<F32>(std::atof(parts[2].c_str())) : 0.f;

            gKRlv.requestForceAdjustHeight(targetPelvisToFoot, factor, delta);
            return true;
        });
}
