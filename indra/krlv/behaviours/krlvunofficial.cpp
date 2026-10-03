/**
 * @file krlvunofficial.cpp
 * @brief KRLV "Unofficial Commands" category - @allowidle.
 *
 * COMPLETE: the one command name in this category.
 *
 * The spec's own wording is a double negative worth reading carefully:
 * "When prevented, the automatic activation of the Away status
 * indicator... CANNOT be disabled." This does NOT mean the restriction
 * stops the avatar from ever going Away - it means the user LOSES the
 * ability to turn the automatic idle-away feature OFF. Confirmed via
 * investigation: `idle_afk_check()` (llappviewer.cpp) already treats an
 * `AFKTimeout` gSavedSettings value of 0 as "auto-away disabled" (the
 * whole `if (afk_timeout() && ...)` check short-circuits false), and the
 * spec's own text states the exact fallback for this case ("If the idle
 * timeout duration has been set to zero, the default timeout of 30
 * minutes will be used"). Gated by substituting 1800 (30 minutes) for a
 * zero `AFKTimeout` at that one chokepoint while `@allowidle=n` is
 * active - the user's own setting is otherwise left completely alone
 * (not overwritten in gSavedSettings, just overridden for this one
 * comparison), and reverts the instant the restriction is lifted.
 *
 * Deliberately scoped to `idle_afk_check()` only, not the separate
 * minimize-the-window-triggers-Away path (`LLViewerWindow`'s own
 * `AFKTimeout` check) - that's a different trigger entirely ("window
 * minimized", not "period of inactivity"), which the spec's own text
 * does not describe.
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
}

void krlv_register_unofficial_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("allowidle",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("allowidle", cmd, sourceId); return true; });
}
