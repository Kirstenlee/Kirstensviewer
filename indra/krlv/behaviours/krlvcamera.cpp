/**
 * @file krlvcamera.cpp
 * @brief KRLV "Camera and view" category.
 *
 * COMPLETE: @camzoommax/@camzoommin (zoom-multiplier bounds), @setcam_fovmin/
 * @setcam_fovmax (direct FOV bounds), @setcam_fov=force (one-shot),
 * @camunlock/@setcam_unlock (camera-lock gate), @getcam_fovmin/
 * @getcam_fovmax/@getcam_zoommin/@getcam_fov (queries), @camdrawmin/
 * @camdrawmax/@camdrawalphamin/@camdrawalphamax/@camdrawcolor (a real
 * distance-based fog-blind shader pass - see KRlv::getCamDrawParams()
 * below and krlv/README.md's Camera section for the full design note;
 * this is the one KRLV category-command family implemented as genuinely
 * new rendering-pipeline code, not a gate on something that already
 * existed - confirmed via investigation that no existing distance-based
 * vision-obscuring effect could be reused, see krlv/README.md), @camavdist
 * (turns avatars beyond a distance into the existing jellydoll "too
 * complex" ghost render - see KRlv::getCamAvDistLimit() below; the
 * jellydoll visual (translucent black + rim glow) already matches the
 * spec's own "visually muted... pitch black" wording, so this reuses
 * LLVOAvatar::isTooComplex() rather than building a new render mode).
 *
 * PARTIAL: @camdistmax/@setcam_avdistmax/@camdistmin/@setcam_avdistmin
 * clamp the camera's distance from the avatar, but do NOT implement the
 * spec's "distance=0 forces Mouselook" / "distance>0 forces OUT of
 * Mouselook and prevents going back" side effects - just the distance
 * clamp itself. @getcam_avdistmin/@getcam_avdistmax are complete queries
 * for the clamp values that ARE tracked.
 *
 * NOT IMPLEMENTED (Stage 2 stub still): @camtextures/@setcam_textures
 * (blanks every in-world texture except worn ones) is genuinely new
 * rendering-pipeline work, not a gate on something that already exists -
 * confirmed via investigation, not assumed: no existing texture-blanking/
 * override mode was found anywhere in dxrender/ or newview/. Left as a
 * stub rather than a shallow, likely-wrong approximation.
 *
 * Synonyms share one internal restriction key each (not the command name
 * used to impose/release them), matching the spec's "exact synonym"
 * wording: @setcam_avdistmax -> "camdistmax", @setcam_avdistmin ->
 * "camdistmin", @setcam_unlock -> "camunlock".
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
#include "krlvcamera.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>

#include "llcamera.h"   // DEFAULT_FIELD_OF_VIEW
#include "llchat.h"

#include "krlvhandler.h"

// KRLV: same plain external-linkage free function krlvversion.cpp
// forward-declares locally.
void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

namespace
{
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

    // Same "=n" adds / "=y" releases (per-source) semantics as every
    // other real category so far - `restrictionKey` lets synonym
    // commands (e.g. @setcam_avdistmax) share one internal key with the
    // command they're a synonym of, rather than using cmd.behaviour.
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

    // The RLVa spec states a multi-source reduction rule PER COMMAND
    // (e.g. "@camzoommax ... retains the smallest value of all") - these
    // are the two shapes that appear; which one applies to which command
    // is decided by the caller, not guessed here.
    bool reduceMin(const std::string& restrictionKey, F32& out)
    {
        std::vector<std::string> options = gKRlv.getRestrictionOptions(restrictionKey);
        if (options.empty())
        {
            return false;
        }
        F32 result = std::numeric_limits<F32>::max();
        for (const std::string& option : options)
        {
            result = std::min(result, static_cast<F32>(std::atof(option.c_str())));
        }
        out = result;
        return true;
    }

    bool reduceMax(const std::string& restrictionKey, F32& out)
    {
        std::vector<std::string> options = gKRlv.getRestrictionOptions(restrictionKey);
        if (options.empty())
        {
            return false;
        }
        F32 result = -std::numeric_limits<F32>::max();
        for (const std::string& option : options)
        {
            result = std::max(result, static_cast<F32>(std::atof(option.c_str())));
        }
        out = result;
        return true;
    }

    // Reply with the reduced value if (and only if) the restriction is
    // actually active - there is no meaningful "default" to report for a
    // restriction nobody has imposed, and fabricating one would be worse
    // than staying silent (matches how a script would need to treat "no
    // reply" from @getstatus-family commands anyway).
    void replyReducedMin(const std::string& restrictionKey, const std::string& channelParam)
    {
        F32 value;
        if (reduceMin(restrictionKey, value))
        {
            replyOnChannel(std::to_string(value), channelParam);
        }
    }

    void replyReducedMax(const std::string& restrictionKey, const std::string& channelParam)
    {
        F32 value;
        if (reduceMax(restrictionKey, value))
        {
            replyOnChannel(std::to_string(value), channelParam);
        }
    }

    // "@camdrawcolor:<red>;<green>;<blue>=<y/n>" - three semicolon-
    // separated floats, 0.0-1.0 per the spec's own text.
    bool parseColorOption(const std::string& option, F32& r, F32& g, F32& b)
    {
        const size_t firstSemi = option.find(';');
        if (firstSemi == std::string::npos)
        {
            return false;
        }
        const size_t secondSemi = option.find(';', firstSemi + 1);
        if (secondSemi == std::string::npos)
        {
            return false;
        }
        r = static_cast<F32>(std::atof(option.substr(0, firstSemi).c_str()));
        g = static_cast<F32>(std::atof(option.substr(firstSemi + 1, secondSemi - firstSemi - 1).c_str()));
        b = static_cast<F32>(std::atof(option.substr(secondSemi + 1).c_str()));
        return true;
    }
}

namespace KRlv
{
    bool getFovBoundsFromZoomMultiplier(F32& outFloorFov, F32& outCeilFov)
    {
        outFloorFov = 0.f;
        outCeilFov = std::numeric_limits<F32>::max();
        bool restricted = false;

        F32 maxMultiplier; // @camzoommax: "retains the smallest value of all"
        if (reduceMin("camzoommax", maxMultiplier))
        {
            outFloorFov = DEFAULT_FIELD_OF_VIEW * maxMultiplier;
            restricted = true;
        }
        F32 minMultiplier; // @camzoommin: "retains the highest value of all"
        if (reduceMax("camzoommin", minMultiplier))
        {
            outCeilFov = DEFAULT_FIELD_OF_VIEW * minMultiplier;
            restricted = true;
        }
        return restricted;
    }

    bool getFovBoundsDirect(F32& outFloorFov, F32& outCeilFov)
    {
        outFloorFov = 0.f;
        outCeilFov = std::numeric_limits<F32>::max();
        bool restricted = false;

        F32 value;
        if (reduceMax("setcam_fovmin", value)) // "retains the greatest value of all"
        {
            outFloorFov = value;
            restricted = true;
        }
        if (reduceMin("setcam_fovmax", value)) // "retains the lowest value of all"
        {
            outCeilFov = value;
            restricted = true;
        }
        return restricted;
    }

    bool getCamDistanceBounds(F32& outFloorDist, F32& outCeilDist)
    {
        outFloorDist = 0.f;
        outCeilDist = std::numeric_limits<F32>::max();
        bool restricted = false;

        F32 value;
        if (reduceMin("camdistmax", value)) // "retains the smallest value of all"
        {
            outCeilDist = value;
            restricted = true;
        }
        if (reduceMax("camdistmin", value)) // "retains the highest value of all"
        {
            outFloorDist = value;
            restricted = true;
        }
        return restricted;
    }

    bool getCamDrawParams(F32& outMinDist, F32& outMaxDist,
                           F32& outMinAlpha, F32& outMaxAlpha,
                           F32& outColorR, F32& outColorG, F32& outColorB)
    {
        F32 minDist = 0.f, maxDist = 0.f;
        const bool hasMin = reduceMin("camdrawmin", minDist); // most restrictive = fog starts as close as possible
        const bool hasMax = reduceMin("camdrawmax", maxDist); // most restrictive = fully fogged as close as possible
        if (!hasMin && !hasMax)
        {
            return false;
        }
        outMinDist = hasMin ? minDist : 0.f;
        outMaxDist = hasMax ? maxDist : (outMinDist + 1.f);

        F32 minAlpha, maxAlpha;
        outMinAlpha = reduceMax("camdrawalphamin", minAlpha) ? minAlpha : 0.f; // most restrictive = most opaque
        outMaxAlpha = reduceMax("camdrawalphamax", maxAlpha) ? maxAlpha : 1.f;

        // "mix of all" per the spec's own explicit text - averaged, not
        // reduced to an extreme like the four bounds above.
        outColorR = 0.f;
        outColorG = 0.f;
        outColorB = 0.f;
        S32 colorCount = 0;
        for (const std::string& option : gKRlv.getRestrictionOptions("camdrawcolor"))
        {
            F32 r, g, b;
            if (parseColorOption(option, r, g, b))
            {
                outColorR += r;
                outColorG += g;
                outColorB += b;
                ++colorCount;
            }
        }
        if (colorCount > 0)
        {
            outColorR /= static_cast<F32>(colorCount);
            outColorG /= static_cast<F32>(colorCount);
            outColorB /= static_cast<F32>(colorCount);
        }
        return true;
    }

    bool isCamTexturesActive(LLUUID& outSubstituteTextureId)
    {
        outSubstituteTextureId.setNull();

        const std::vector<std::string> options = gKRlv.getRestrictionOptions("camtextures");
        if (options.empty())
        {
            return false;
        }

        for (const std::string& option : options)
        {
            if (!option.empty())
            {
                LLUUID id;
                if (id.set(option, false) && id.notNull())
                {
                    outSubstituteTextureId = id;
                    break;
                }
            }
        }
        return true;
    }

    bool getCamAvDistLimit(F32& outDistance)
    {
        return reduceMin("camavdist", outDistance);
    }
}

void krlv_register_camera_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("camzoommax",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camzoommax", cmd, sourceId); return true; });

    handler.registerBehaviour("camzoommin",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camzoommin", cmd, sourceId); return true; });

    handler.registerBehaviour("setcam_fovmin",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("setcam_fovmin", cmd, sourceId); return true; });

    handler.registerBehaviour("setcam_fovmax",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("setcam_fovmax", cmd, sourceId); return true; });

    handler.registerBehaviour("setcam_fov",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            if (cmd.hasOption())
            {
                gKRlv.requestForceFov(static_cast<F32>(std::atof(cmd.option.c_str())));
            }
            return true;
        });

    handler.registerBehaviour("camdistmax",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdistmax", cmd, sourceId); return true; });
    handler.registerBehaviour("setcam_avdistmax",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdistmax", cmd, sourceId); return true; }); // exact synonym

    handler.registerBehaviour("camdistmin",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdistmin", cmd, sourceId); return true; });
    handler.registerBehaviour("setcam_avdistmin",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdistmin", cmd, sourceId); return true; }); // exact synonym

    handler.registerBehaviour("camunlock",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camunlock", cmd, sourceId); return true; });
    handler.registerBehaviour("setcam_unlock",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camunlock", cmd, sourceId); return true; }); // exact synonym

    handler.registerBehaviour("getcam_avdistmin",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { replyReducedMax("camdistmin", cmd.param); return true; });
    handler.registerBehaviour("getcam_avdistmax",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { replyReducedMin("camdistmax", cmd.param); return true; });
    handler.registerBehaviour("getcam_fovmin",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { replyReducedMax("setcam_fovmin", cmd.param); return true; });
    handler.registerBehaviour("getcam_fovmax",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { replyReducedMin("setcam_fovmax", cmd.param); return true; });
    handler.registerBehaviour("getcam_zoommin",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { replyReducedMax("camzoommin", cmd.param); return true; });
    handler.registerBehaviour("getcam_fov",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            replyOnChannel(std::to_string(gKRlv.requestGetFov()), cmd.param);
            return true;
        });

    // KRLV_TOUCHPOINT: @camdrawmin/@camdrawmax/@camdrawalphamin/
    // @camdrawalphamax/@camdrawcolor - a distance-based fog-blind effect,
    // implemented as a real HLSL shader pass (KRlv::getCamDrawParams()
    // above, consumed by DXPipeline::presentDeferredScreen() /
    // LLPipeline::applyKrlvCamDrawFog()) rather than a gate on an
    // existing function - see krlv/README.md's Camera section.
    handler.registerBehaviour("camdrawmin",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdrawmin", cmd, sourceId); return true; });
    handler.registerBehaviour("camdrawmax",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdrawmax", cmd, sourceId); return true; });
    handler.registerBehaviour("camdrawalphamin",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdrawalphamin", cmd, sourceId); return true; });
    handler.registerBehaviour("camdrawalphamax",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdrawalphamax", cmd, sourceId); return true; });
    handler.registerBehaviour("camdrawcolor",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camdrawcolor", cmd, sourceId); return true; });

    // KRLV_TOUCHPOINT: @camavdist - reuses the existing jellydoll
    // ("too complex") ghost render rather than a new solid-silhouette
    // mode - see KRlv::getCamAvDistLimit() above and krlv/README.md's
    // Camera section.
    handler.registerBehaviour("camavdist",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camavdist", cmd, sourceId); return true; });

    // KRLV_TOUCHPOINT: @camtextures/@setcam_textures - blanks in-world
    // textures via a stencil-gated extension of the SAME fog-blind pass
    // above (DXPipeline::presentDeferredScreen()/krlvCamDrawFogF.hlsl),
    // not a separate pipeline stage - see KRlv::isCamTexturesActive()
    // above and krlv/README.md's Camera section for the full design note.
    handler.registerBehaviour("camtextures",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camtextures", cmd, sourceId); return true; });
    handler.registerBehaviour("setcam_textures",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("camtextures", cmd, sourceId); return true; }); // exact synonym
}
