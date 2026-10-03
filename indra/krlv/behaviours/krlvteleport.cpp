/**
 * @file krlvteleport.cpp
 * @brief KRLV "Teleportation" category.
 *
 * COMPLETE: @tplm, @tploc (which, via the shared chokepoint it gates,
 * also naturally satisfies the spec's "@tpto is inhibited by @tploc=n"
 * relationship - no extra code needed for that), @sittp, @tplure/
 * @tplure_sec (decline half only - see below), @tprequest/
 * @tprequest_sec (decline half only), @accepttp, @accepttprequest,
 * @standtp (see below).
 *
 * PARTIAL: @tplocal (the distance clamp works; does not implement the
 * spec's "prevents by double-clicking" scoping any more precisely than
 * "any same-region teleport request" - see krlv/README.md). @tplure_sec/
 * @tprequest_sec share their plain command's restriction key - the
 * spec's "_sec" nuance (exceptions only honoured from the SAME object
 * that imposed the restriction) is NOT implemented; any exception UUID
 * is honoured regardless of which object added it. @tprequest's decline
 * half is silent (no "that other user receives a message" reply was
 * found to safely reuse). @tpto=force's region-name form (below) and its
 * ";lookat" suffix are also partial.
 *
 * @tpto=force is COMPLETE for the GLOBAL-coordinate syntax
 * ("@tpto:<X>/<Y>/<Z>=force"). The region-name+local-coordinates syntax
 * ("@tpto:<region_name>/<X>/<Y>/<Z>[;lookat]=force") IS implemented, via
 * KRlvResolveRegionPositionRequest (krlvhandler.h) - but only resolves a
 * region name the viewer already knows about (the current region or an
 * already-loaded neighbour, via LLWorld::getRegionList()); there is no
 * synchronous grid-wide name lookup, only an asynchronous capability-
 * based LLWorldMap query, which doesn't fit this module's synchronous
 * command-handling shape without real redesign. A region name the
 * viewer hasn't seen this session silently fails to resolve, same as
 * any other malformed/unsupported KRLV command - not guessed at. The
 * optional ";lookat" suffix is parsed off (so it doesn't break the Z
 * coordinate) but not applied - no force-teleport path in this module
 * takes a look-at direction yet.
 *
 * @standtp=<y/n> is COMPLETE: krlv/'s own idle tick (registerIdleTick(),
 * krlvhandler.h) polls sit-state and position every frame via
 * KRlvGetAgentStateRequest - this sidesteps the original concern (racing
 * the server's async confirmation of a stand-up request) entirely, since
 * polling only cares about NOTICING a local state transition, not WHEN
 * it happens relative to the request that caused it. The "last standing
 * location" anchor is captured at sit-time, and re-captured immediately
 * if the restriction is imposed while already sitting (the spec's own
 * stated anti-exploit clause - see pollStandTp()/the "standtp" handler
 * below) - not just a simplification of convenience, a literal reading
 * of the spec's text.
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
#include "krlvteleport.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

#include "llerror.h"
#include "lluuid.h"
#include "v3dmath.h"
#include "v3math.h"

#include "krlvhandler.h"

namespace
{
    // Same "=n" adds / "=y" releases (per-source) semantics as every
    // other real category so far.
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

    // "@behaviour[:<UUID>]=<rem/add>" family (tplure/accepttp/
    // accepttprequest/tprequest all share this shape): no option ->
    // toggles a GLOBAL on-switch keyed by the imposing source; a UUID
    // option -> toggles a per-sender exception/allow-list, stored under
    // exceptionKey(perUuidKey, uuid) with the imposing object as source,
    // so a lookup by sender id is a plain hasRestrictionFrom() check.
    void applyAddRemToggle(const std::string& allKey, const std::string& perUuidKey,
        const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        if (cmd.hasOption())
        {
            LLUUID target;
            if (!target.set(cmd.option, false) || target.isNull())
            {
                return;
            }
            if (cmd.param == "add")
            {
                gKRlv.addRestriction(KRlvHandler::exceptionKey(perUuidKey, target), sourceId);
            }
            else if (cmd.param == "rem")
            {
                gKRlv.removeRestriction(KRlvHandler::exceptionKey(perUuidKey, target), sourceId);
            }
        }
        else
        {
            if (cmd.param == "add")
            {
                gKRlv.addRestriction(allKey, sourceId);
            }
            else if (cmd.param == "rem")
            {
                gKRlv.removeRestriction(allKey, sourceId);
            }
        }
    }

    // @sittp/@tplocal share this "minimum distance of all, 0 (no
    // exception) if any source omits a distance" shape - see
    // krlvteleport.h for which parts of this are spec-stated vs. a
    // documented engineering choice.
    bool getDistanceLimit(const std::string& restrictionKey, F32 defaultDistance, F32& outMaxDist)
    {
        std::vector<std::string> options = gKRlv.getRestrictionOptions(restrictionKey);
        if (options.empty())
        {
            return false;
        }
        F32 result = std::numeric_limits<F32>::max();
        bool anyExplicit = false;
        for (const std::string& option : options)
        {
            if (option.empty())
            {
                // A source restricting with no distance at all means
                // "no exception, period" - most restrictive possible,
                // short-circuits the whole reduction.
                outMaxDist = 0.f;
                return true;
            }
            result = std::min(result, static_cast<F32>(std::atof(option.c_str())));
            anyExplicit = true;
        }
        outMaxDist = anyExplicit ? result : defaultDistance;
        return true;
    }

    // "<X>/<Y>/<Z>" - the GLOBAL-coordinate @tpto syntax only; see the
    // file header for why the region-name syntax isn't handled here.
    bool parseGlobalCoords(const std::string& option, LLVector3d& outPos)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= option.size())
        {
            size_t slash = option.find('/', start);
            if (slash == std::string::npos)
            {
                parts.push_back(option.substr(start));
                break;
            }
            parts.push_back(option.substr(start, slash - start));
            start = slash + 1;
        }
        if (parts.size() != 3)
        {
            return false;
        }
        outPos.mdV[VX] = std::atof(parts[0].c_str());
        outPos.mdV[VY] = std::atof(parts[1].c_str());
        outPos.mdV[VZ] = std::atof(parts[2].c_str());
        return true;
    }

    // "<region_name>/<X_local>/<Y_local>/<Z_local>[;lookat]" - the
    // region-name+local-coordinates @tpto syntax. Region names never
    // contain '/', so a plain split is unambiguous; the optional
    // ";lookat" suffix on the Z component is parsed off and discarded -
    // NOT implemented (no existing force-teleport path in this module
    // takes a look-at direction), same as this file's other documented
    // simplifications.
    bool parseRegionLocalCoords(const std::string& option, std::string& outRegionName, LLVector3& outLocalPos)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= option.size())
        {
            size_t slash = option.find('/', start);
            if (slash == std::string::npos)
            {
                parts.push_back(option.substr(start));
                break;
            }
            parts.push_back(option.substr(start, slash - start));
            start = slash + 1;
        }
        if (parts.size() != 4 || parts[0].empty())
        {
            return false;
        }

        std::string zPart = parts[3];
        const size_t semi = zPart.find(';');
        if (semi != std::string::npos)
        {
            zPart = zPart.substr(0, semi);
        }

        outRegionName = parts[0];
        outLocalPos.mV[VX] = static_cast<F32>(std::atof(parts[1].c_str()));
        outLocalPos.mV[VY] = static_cast<F32>(std::atof(parts[2].c_str()));
        outLocalPos.mV[VZ] = static_cast<F32>(std::atof(zPart.c_str()));
        return true;
    }

    // @standtp state - see krlv_register_teleport_commands()'s
    // registerIdleTick() call and the "standtp" behaviour handler below,
    // both of which read/write these. File-local, not KRlvHandler state:
    // this restriction is the only thing in the whole module that needs
    // continuous per-frame tracking, so there is no reason to make the
    // core handler know about it.
    bool sStandTpWasSitting = false;
    LLVector3d sStandTpLastStandingPos;
    LLVector3d sStandTpAnchor;
    bool sStandTpHasAnchor = false;

    // Polled every idle tick (registerIdleTick()) regardless of whether
    // @standtp is currently active, so sStandTpLastStandingPos is always
    // fresh the moment it IS activated (or the moment the avatar next
    // sits down) - the alternative, only tracking while restricted, would
    // miss the position if @standtp gets added and then the avatar sits
    // and stands within a frame or two of it appearing.
    void pollStandTp()
    {
        LLVector3d pos;
        const bool isSitting = gKRlv.requestGetAgentState(pos);
        const bool restricted = gKRlv.isRestricted("standtp");

        if (isSitting && !sStandTpWasSitting && restricted)
        {
            // Just sat down while restricted - "the location where it
            // initially sat down" (spec's own wording).
            sStandTpAnchor = sStandTpLastStandingPos;
            sStandTpHasAnchor = true;
        }
        else if (!isSitting && sStandTpWasSitting && restricted && sStandTpHasAnchor)
        {
            // Just stood up while restricted - enforce.
            gKRlv.requestForceTeleport(sStandTpAnchor);
            sStandTpHasAnchor = false;
        }

        if (!isSitting)
        {
            sStandTpLastStandingPos = pos;
        }
        sStandTpWasSitting = isSitting;
    }
}

namespace KRlv
{
    bool getSitDistanceLimit(F32& outMaxDist)
    {
        // Spec's own stated default and reduction rule for @sittp.
        return getDistanceLimit("sittp", 1.5f, outMaxDist);
    }

    bool getLocalTeleportDistanceLimit(F32& outMaxDist)
    {
        // No default/reduction rule stated for @tplocal specifically -
        // 0.f (no exception) matches "no distance specified" for
        // consistency with @sittp; see krlvteleport.h.
        return getDistanceLimit("tplocal", 0.f, outMaxDist);
    }
}

void krlv_register_teleport_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("tplm",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("tplm", cmd, sourceId); return true; });

    handler.registerBehaviour("tploc",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("tploc", cmd, sourceId); return true; });

    handler.registerBehaviour("tplocal",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("tplocal", cmd, sourceId); return true; });

    handler.registerBehaviour("sittp",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sittp", cmd, sourceId); return true; });

    handler.registerBehaviour("tplure",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.hasOption())
            {
                LLUUID target;
                if (target.set(cmd.option, false) && target.notNull())
                {
                    if (cmd.param == "add") gKRlv.addRestriction(KRlvHandler::exceptionKey("tplure_except", target), sourceId);
                    else if (cmd.param == "rem") gKRlv.removeRestriction(KRlvHandler::exceptionKey("tplure_except", target), sourceId);
                }
            }
            else
            {
                applyToggle("tplure", cmd, sourceId);
            }
            return true;
        });
    handler.registerBehaviour("tplure_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("tplure", cmd, sourceId); return true; }); // shares "tplure" key - _sec nuance not implemented

    handler.registerBehaviour("tprequest",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.hasOption())
            {
                LLUUID target;
                if (target.set(cmd.option, false) && target.notNull())
                {
                    if (cmd.param == "add") gKRlv.addRestriction(KRlvHandler::exceptionKey("tprequest_except", target), sourceId);
                    else if (cmd.param == "rem") gKRlv.removeRestriction(KRlvHandler::exceptionKey("tprequest_except", target), sourceId);
                }
            }
            else
            {
                applyToggle("tprequest", cmd, sourceId);
            }
            return true;
        });
    handler.registerBehaviour("tprequest_sec",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("tprequest", cmd, sourceId); return true; }); // shares "tprequest" key - _sec nuance not implemented

    handler.registerBehaviour("accepttp",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyAddRemToggle("accepttp_all", "accepttp_from", cmd, sourceId); return true; });

    handler.registerBehaviour("accepttprequest",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyAddRemToggle("accepttprequest_all", "accepttprequest_from", cmd, sourceId); return true; });

    handler.registerBehaviour("tpto",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            LLVector3d posGlobal;
            if (parseGlobalCoords(cmd.option, posGlobal))
            {
                gKRlv.requestForceTeleport(posGlobal);
                return true;
            }

            std::string regionName;
            LLVector3 localPos;
            if (parseRegionLocalCoords(cmd.option, regionName, localPos)
                && gKRlv.requestResolveRegionPosition(regionName, localPos, posGlobal))
            {
                gKRlv.requestForceTeleport(posGlobal);
            }
            // Else: malformed, or a region name not currently known to
            // the viewer - silently ignored, same as any other malformed
            // KRLV command (see file header for the region-lookup scope
            // limit).
            return true;
        });

    // KRLV_TOUCHPOINT: @standtp - pure state observation via idle-tick
    // polling (pollStandTp() above), no viewer-side gate needed beyond
    // the KRlvGetAgentStateRequest/KRlvForceTeleportRequest hooks - see
    // krlv/README.md's Teleportation section.
    handler.registerBehaviour("standtp",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "n")
            {
                gKRlv.addRestriction("standtp", sourceId, cmd.option);
                // Anti-exploit clause from the spec's own text: refresh
                // the anchor to the CURRENT position immediately if the
                // avatar is already sitting when the restriction is
                // imposed (e.g. sat, moved while seated, THEN
                // restricted) - without this, they could ride back out
                // to wherever they originally sat down instead of
                // staying put.
                LLVector3d pos;
                if (gKRlv.requestGetAgentState(pos))
                {
                    sStandTpAnchor = pos;
                    sStandTpHasAnchor = true;
                }
            }
            else if (cmd.param == "y")
            {
                gKRlv.removeRestriction("standtp", sourceId);
            }
            return true;
        });
    handler.registerIdleTick(pollStandTp);
}
