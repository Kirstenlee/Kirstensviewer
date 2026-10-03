/**
 * @file krlvviewercontrol.cpp
 * @brief KRLV "Viewer Control" category.
 *
 * @setdebug_<setting>/@getdebug_<setting> are COMPLETE - force-set/read a
 * gSavedSettings debug setting by name, mirroring the S24 Console's own
 * "set"/"get" commands (kvdebugconsole.cpp's processSetCommand()/
 * processGetCommand()) but implemented independently inside krlv/, since
 * krlv/ cannot call into newview code (that dependency only runs the
 * other way). Both use ONLY llcontrol.h (llxml, already a krlv
 * dependency) - no new touchpoint needed.
 *
 * @setenv_<setting>:<value>=force / @getenv_<setting> are COMPLETE for
 * 37 of the spec's legacy Windlight parameter names - one hook pair
 * (KRlvApplyForcedEnvironmentRequest/KRlvGetLiveEnvironmentSettingRequest,
 * krlvhandler.h) carries plain name/float pairs to newview, where ALL
 * name<->LLSettingsSky field translation lives (llappviewer.cpp) since
 * krlv/ cannot link llsettingssky.h/llenvironment.h. krlv/ itself only
 * ever: (1) checks the incoming name against kRecognizedSettings below -
 * anything NOT on that list (a genuine spec gap like "preset"/the "i"
 * intensity components, a typo, or a name this module hasn't researched)
 * raises the KRLVStubHit toast+chat notice (its own function here, not a
 * shared stub), per the user's explicit request to alert on a setting a
 * real (legacy-device) object sends that truly has no equivalent here,
 * rather than silently no-op; (2) stores the name/value in sForcedEnvParams and
 * pushes it newview-side via requestApplyForcedEnvironment(); (3) for
 * @getenv_, answers from sForcedEnvParams directly if already forced,
 * else asks newview for a live read via requestGetLiveEnvironmentSetting().
 *
 * Confirmed via direct research against THIS codebase's own settings
 * API (S:\Dev\S24\indra\llinventory\llsettingssky.h/.cpp) rather than
 * assumed: S24 still carries a full, real "legacy haze" sub-model
 * (getBlueHorizon()/setBlueHorizon() etc., mHasLegacyHaze,
 * translateLegacySettings()) alongside the modern Rayleigh/Mie
 * atmosphere - NOT superseded for blue_horizon/haze_density/etc as
 * originally assumed; those map directly. Two parameters use a
 * documented, derived-not-confirmed approximation rather than a
 * fabricated "confirmed" mapping (same engineering-judgment class as
 * Movement's @setrot/@adjustheight):
 *   - "sunglowfocus"/"sunglowsize" pack into one LLColor3 (mGlow) with
 *     no conversion formula found anywhere in this codebase's history -
 *     applied as a direct, unscaled passthrough into 2 of its 3
 *     channels.
 *   - "daytime" has no synchronous day-position API reachable outside
 *     the day-cycle EDITOR's own internal blender objects
 *     (llfloatereditextdaycycle.cpp) - approximated as a sine arc on
 *     sun/moon altitude ONLY (azimuth left untouched), calibrated to the
 *     spec's two unambiguous horizon-crossing anchors (sunrise=0.25,
 *     sunset=0.75 -> zero altitude); the spec's stated midday=0.567
 *     (not this model's 0.5 peak) is a known, documented divergence, not
 *     silently assumed correct.
 * One confirmed real discrepancy, resolved in the codebase's own favour:
 * "cloudscrollx"/"cloudscrolly" get the same -10.0 legacy offset
 * translateLegacySettings() itself already applies when importing old
 * Windlight assets, even though the spec text's stated 0.0-1.0 range
 * looks inconsistent with that - trusting this codebase's own real,
 * working conversion over a secondary-sourced spec range reading.
 *
 * Self-healing: requestApplyForcedEnvironment() runs once immediately on
 * every real @setenv_<setting>=force (matching spec - a one-shot push,
 * no @setenv=n needed), AND, separately, every idle tick
 * (pollForcedEnvironment() below) while @setenv is ALSO restricted - the
 * "sticky, forced-shared-environment" behaviour the user asked for when
 * force + lock are used together. The hook always re-derives from the
 * LIVE current sky (LLEnvironment::getCurrentSky(), never a frozen
 * snapshot), so untouched parameters keep tracking reality (day/night
 * motion, parcel/region changes) instead of freezing at whatever they
 * were when first forced. This also plugs a real, independently-
 * confirmed gap in @setenv=n's own menu-chokepoint enforcement:
 * LLAgent's teleport-complete handler calls
 * LLEnvironment::setSharedEnvironment() directly (gated behind
 * "SwitchToSharedEnvAfterTeleport", default true), completely bypassing
 * LLWorldEnvSettings::handleEvent() - without this reapplication, a
 * teleport would silently clear a locked, forced environment with no
 * code path to catch it. sForcedEnvParams is NOT cleared when @setenv is
 * released (=y) - "force" and "restrict" are independent, orthogonal
 * command families in the spec; releasing the lock stops the continuous
 * reapplication but deliberately leaves whatever was last forced in
 * place, matching how no other force command in this module has an
 * "unforce" either.
 *
 * @setenv (the plain y/n form) is COMPLETE as a real restriction - unlike
 * @setdebug (below), the spec is explicit about exactly what it locks:
 * "World > Environment Settings > Sunrise/Midday/Sunset/Midnight/Revert
 * to region default/Environment editor are all locked out". Every one of
 * those is already funnelled through ONE existing chokepoint -
 * LLWorldEnvSettings::handleEvent() (llviewermenu.cpp), the single
 * on_click handler behind the World > Environment submenu's time-of-day
 * items, "Use Shared Environment", "My Environments...", and "Personal
 * Lighting..." (the closest living equivalent to the spec's "Environment
 * editor" floater - the real Day Cycle editor is only ever reached
 * through those two, so gating them transitively covers it too, no
 * second touchpoint needed). "pause_clouds" is deliberately exempt - it's
 * a cosmetic animation toggle, not an environment override, and the spec
 * doesn't name it. This stores and queries via `gKRlv.isRestricted
 * ("setenv")` like any other plain toggle - no new hook or UI-enable
 * machinery added, matching how @fly/@temprun/@alwaysrun gate their own
 * chokepoints (a restricted click silently no-ops rather than greying
 * out the menu item - consistent with this codebase's existing
 * restrictions, not a new UX convention). Known gap: any OTHER path into
 * the day-cycle editor that doesn't route through handleEvent() (none
 * found in this pass) would not be covered - flagged here rather than
 * silently assumed complete.
 *
 * @setdebug (the plain y/n form) is COMPLETE as a real restriction, with
 * a REAL enforcement gate - but, unlike @setenv above, deliberately
 * NOT spec-faithful in content, only in shape, because it genuinely
 * cannot be: the real spec says it locks a SMALL, UNDOCUMENTED whitelist
 * of Debug Settings floater rows, and neither the whitelist itself nor
 * which rows map to it are published anywhere this clean-room module can
 * read (see the licence header below). Rather than guess at RLVa's
 * internal list, this builds KRLV's OWN user-configured whitelist
 * instead - persisted separately (krlv_protected_debug.dat, see
 * krlvpersist.h), managed via isDebugSettingProtected()/
 * addProtectedDebugSetting()/removeProtectedDebugSetting()/
 * getProtectedDebugSettings() (krlvhandler.h). `@setdebug=n` locks
 * editing of whatever settings are CURRENTLY on that list (not "all debug
 * settings", matching the spec's own "whitelist, not everything" shape);
 * an empty list (the default - nothing is protected until the user adds
 * something) means the restriction, while real and trackable via
 * @getstatus etc, has nothing to actually enforce yet.
 *
 * Real enforcement gate (not just plumbing): newview/
 * llfloatersettingsdebug.cpp's LLFloaterSettingsDebug::onCommitSettings()/
 * onClickDefault() - the Debug Settings floater's own two value-changing
 * entry points (committing an edited value, resetting to default) - both
 * refuse when `gKRlv.isRestricted("setdebug") &&
 * gKRlv.isDebugSettingProtected(controlp->getName())`.
 *
 * User-facing management is CONSOLE-ONLY for now ("krlv protectdebug
 * [add|remove <name>]", mirroring the blacklist's own
 * console-until-a-floater-exists bridge) - a real settings-picker floater
 * is explicitly deferred to a later pass; when built, it should call the
 * same 4 KRlvHandler methods above rather than grow its own storage.
 *
 * Known open question, not yet resolved: S24's own debug console
 * (kvdebugconsole.cpp) is not a feature any stock RLV-capable viewer has,
 * so the original RLVa spec has nothing to say about whether an object
 * should be able to reach it, or be blocked from reaching it, via these
 * commands. For now the one thing enforced is the KRLV-prefix rule below -
 * no KRLV setting (any name starting with "KRLV", case-insensitive) can be
 * force-set or read via @setdebug_ / @getdebug_, regardless of what else
 * this category eventually covers - so a restricting object can never
 * switch KRLV off, or read its own state, from the inside.
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

#include <array>
#include <cstdlib>
#include <map>

#include "llerror.h"
#include "llchat.h"
#include "llcontrol.h"
#include "llnotificationsutil.h"
#include "llsd.h"
#include "llstring.h"
#include "lltimer.h"

#include "krlvhandler.h"

// KRLV: same plain external-linkage free function krlvversion.cpp
// forward-declares locally.
void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

extern LLControlGroup gSavedSettings;

namespace
{
    // KRLV: every KRLV setting is off limits to @setdebug_ and @getdebug_. Matched by
    // prefix, case-insensitively, so no object can reach a KRLV setting by changing case.
    constexpr char kKrlvSettingPrefix[] = "krlv";
    constexpr char kDebugRefusedText[] = "REFUSED";

    bool isProtectedSetting(const std::string& name)
    {
        return name.size() >= sizeof(kKrlvSettingPrefix) - 1
            && LLStringUtil::compareInsensitive(name.substr(0, sizeof(kKrlvSettingPrefix) - 1), kKrlvSettingPrefix) == 0;
    }

    // Same "=n" adds / "=y" releases (per-source) semantics as
    // krlvmovement.cpp's/krlvlocation.cpp's applyToggle() - duplicated
    // locally rather than shared, consistent with this project's
    // one-file-per-category style.
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

    // KRLV: mirrors kvdebugconsole.cpp's processSetCommand() type-coercion
    // rules (same boolean spellings, same numeric parsing) for consistent
    // behaviour between the console and RLVa force-set - written
    // independently since krlv/ cannot call into newview code.
    void forceSetDebugSetting(const std::string& settingName, const std::string& value)
    {
        if (isProtectedSetting(settingName))
        {
            LL_DEBUGS("KRLV") << "Refused @setdebug_" << settingName
                << " - this setting is protected from RLVa force-set." << LL_ENDL;
            return;
        }

        LLControlVariablePtr control = gSavedSettings.getControl(settingName);
        if (!control)
        {
            // Unknown setting name - silently ignored, same as any other
            // unrecognized KRLV input.
            return;
        }

        switch (control->type())
        {
            case TYPE_BOOLEAN:
            {
                std::string lower = value;
                LLStringUtil::toLower(lower);
                if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
                {
                    gSavedSettings.setBOOL(settingName, TRUE);
                }
                else if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
                {
                    gSavedSettings.setBOOL(settingName, FALSE);
                }
                break;
            }
            case TYPE_S32:
                gSavedSettings.setS32(settingName, std::atoi(value.c_str()));
                break;
            case TYPE_U32:
                gSavedSettings.setU32(settingName, static_cast<U32>(std::strtoul(value.c_str(), nullptr, 10)));
                break;
            case TYPE_F32:
                gSavedSettings.setF32(settingName, static_cast<F32>(std::atof(value.c_str())));
                break;
            case TYPE_STRING:
            {
                std::string clean = value;
                if (clean.size() >= 2 && clean.front() == '"' && clean.back() == '"')
                {
                    clean = clean.substr(1, clean.size() - 2);
                }
                gSavedSettings.setString(settingName, clean);
                break;
            }
            default:
                // Other control types (LLSD/Vector3/Color4/...) not
                // supported for RLVa force-set in this pass.
                break;
        }
    }

    std::string readDebugSetting(const std::string& settingName)
    {
        if (isProtectedSetting(settingName))
        {
            LL_DEBUGS("KRLV") << "Refused @getdebug_" << settingName
                << " - this setting is protected from RLVa read." << LL_ENDL;
            return kDebugRefusedText;
        }

        LLControlVariablePtr control = gSavedSettings.getControl(settingName);
        if (!control)
        {
            return std::string();
        }
        switch (control->type())
        {
            case TYPE_BOOLEAN:
                return control->get().asBoolean() ? "TRUE" : "FALSE";
            case TYPE_S32:
            case TYPE_U32:
                return std::to_string(control->get().asInteger());
            case TYPE_F32:
                return std::to_string(control->get().asReal());
            default:
                return control->get().asString();
        }
    }

    // KRLV: every @setenv_<setting>/@getenv_<setting> name this module
    // recognizes - see the file header for the research behind this
    // exact list (37 names: confirmed direct mappings, plus "daytime"
    // and "sunglowfocus"/"sunglowsize" which newview-side applies as a
    // documented approximation, not a confirmed one). Anything NOT on
    // this list - a genuine spec gap ("preset", the "i" intensity
    // components), a typo, or simply a name this module hasn't
    // researched - is treated the same way: notify, don't silently
    // no-op. Deliberately one flat list rather than category-grouped;
    // nothing here needs to know WHICH kind of parameter a name is, only
    // whether it's recognized at all.
    constexpr std::array<const char*, 37> kRecognizedEnvironmentSettings =
    {
        "daytime", "scenegamma", "starbrightness", "densitymultiplier",
        "distancemultiplier", "hazedensity", "hazehorizon", "maxaltitude",
        "cloudscale", "cloudcoverage",
        "ambientr", "ambientg", "ambientb",
        "bluedensityr", "bluedensityg", "bluedensityb",
        "bluehorizonr", "bluehorizong", "bluehorizonb",
        "cloudcolorr", "cloudcolorg", "cloudcolorb",
        "sunmooncolorr", "sunmooncolorg", "sunmooncolorb",
        "cloudx", "cloudy", "cloudd",
        "clouddetailx", "clouddetaily", "clouddetaild",
        "cloudscrollx", "cloudscrolly",
        "sunglowfocus", "sunglowsize",
        "eastangle", "sunmoonposition",
    };

    // Must match krlvhandler.cpp's own KRLV_DEBUG_CHANNEL constant -
    // duplicated locally rather than shared, consistent with this
    // project's one-file-per-category style (see applyToggle() above).
    constexpr S32 kEnvDebugChannel = -19788215;

    bool isRecognizedEnvironmentSetting(const std::string& name)
    {
        for (const char* recognized : kRecognizedEnvironmentSettings)
        {
            if (name == recognized)
            {
                return true;
            }
        }
        return false;
    }

    // KRLV: the sparse "what's currently forced" patch @setenv_<setting>
    // builds up and pollForcedEnvironment() keeps re-pushing - see the
    // file header's "Self-healing" paragraph. File-local, not
    // KRlvHandler state, same reasoning as krlvteleport.cpp's @standtp
    // statics: this is the only thing in this module needing continuous
    // per-frame tracking.
    std::map<std::string, F32> sForcedEnvParams;

    // LLTimer seconds before which pollForcedEnvironment() does nothing.
    F64 sNextEnvPollAt = 0.0;

    // KRLV: toast + debug-channel chat notice for an environment setting with no
    // equivalent here (KRLVStubHit, skins/default/xui/en/notifications.xml). Reuses
    // that template, since "not yet implemented in KRLV" is equally true of it.
    void notifyUnsupportedEnvironmentSetting(const std::string& settingName, const std::string& value)
    {
        std::string fullName = "setenv_" + settingName;
        if (!value.empty())
        {
            fullName += ":" + value;
        }

        LL_DEBUGS("KRLV") << "@setenv_" << settingName
            << " has no equivalent in this viewer - see krlvviewercontrol.cpp's"
            << " file header for the full researched parameter list." << LL_ENDL;

        LLSD args;
        args["COMMAND"] = fullName;
        LLNotificationsUtil::add("KRLVStubHit", args);

        send_chat_from_viewer(
            "ALERT! RLVa command detected: @" + fullName + " - not yet implemented in KRLV.",
            CHAT_TYPE_SHOUT, kEnvDebugChannel);
    }

    // KRLV: @setenv's self-heal - see the file header's "Self-healing"
    // paragraph. Polled every idle tick regardless of whether anything
    // is currently forced (cheap early-out below), so a force command
    // that arrives before any @setenv=n lock, or a lock that arrives
    // before any force command, both still end up self-healing the
    // moment both conditions are true.
    void pollForcedEnvironment()
    {
        if (sForcedEnvParams.empty() || !gKRlv.isRestricted("setenv"))
        {
            return;
        }
        // At most once per second: re-applying the environment every frame is needless cost.
        const F64 now = LLTimer::getElapsedSeconds();
        if (now < sNextEnvPollAt)
        {
            return;
        }
        sNextEnvPollAt = now + 1.0;
        gKRlv.requestApplyForcedEnvironment(sForcedEnvParams);
    }
}

void krlv_register_viewer_control_commands(KRlvHandler& handler)
{
    // "@setdebug=<y/n>" - see file header: a real restriction, enforced
    // at LLFloaterSettingsDebug::onCommitSettings()/onClickDefault()
    // (newview/llfloatersettingsdebug.cpp) for whatever settings are on
    // KRLV's own user-configured protected list (empty by default).
    handler.registerBehaviour("setdebug",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyToggle(cmd, sourceId);
            return true;
        });

    // "@setenv=<y/n>" - see file header: a plain restriction, enforced at
    // the one existing chokepoint behind the whole World > Environment
    // submenu (LLWorldEnvSettings::handleEvent(), llviewermenu.cpp).
    handler.registerBehaviour("setenv",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            applyToggle(cmd, sourceId);
            return true;
        });

    // "@setdebug_<setting>:<value>=force" - setting name is glued onto
    // the prefix (no ":option" separator), so it's recovered from
    // cmd.behaviourRaw (case-preserved), not cmd.behaviour (lowercased,
    // and gSavedSettings control lookups are case-sensitive).
    handler.registerBehaviourPrefix("setdebug_",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            static const std::string kPrefix = "setdebug_";
            if (cmd.behaviourRaw.size() <= kPrefix.size())
            {
                return true;
            }
            const std::string settingName = cmd.behaviourRaw.substr(kPrefix.size());
            forceSetDebugSetting(settingName, cmd.option);
            return true;
        });

    handler.registerBehaviourPrefix("getdebug_",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            static const std::string kPrefix = "getdebug_";
            if (cmd.behaviourRaw.size() <= kPrefix.size())
            {
                return true;
            }
            const std::string settingName = cmd.behaviourRaw.substr(kPrefix.size());
            replyOnChannel(readDebugSetting(settingName), cmd.param);
            return true;
        });

    // "@setenv_<setting>:<value>=force" - see file header. cmd.behaviour
    // (lowercased) is used for the name, not cmd.behaviourRaw - unlike
    // @setdebug_, every recognized name here is a fixed, known-lowercase
    // spec string, not an arbitrary case-sensitive gSavedSettings name.
    handler.registerBehaviourPrefix("setenv_",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            static const std::string kPrefix = "setenv_";
            if (cmd.behaviour.size() <= kPrefix.size())
            {
                return true;
            }
            const std::string settingName = cmd.behaviour.substr(kPrefix.size());

            if (!isRecognizedEnvironmentSetting(settingName))
            {
                notifyUnsupportedEnvironmentSetting(settingName, cmd.option);
                return true;
            }

            const F32 value = static_cast<F32>(std::atof(cmd.option.c_str()));
            sForcedEnvParams[settingName] = value;

            // Always apply once immediately - matches the spec's own
            // one-shot "force" semantics, no @setenv=n required. The
            // idle-tick poll below additionally re-asserts this every
            // frame while @setenv IS also restricted - see file header.
            gKRlv.requestApplyForcedEnvironment(sForcedEnvParams);
            return true;
        });

    handler.registerBehaviourPrefix("getenv_",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            static const std::string kPrefix = "getenv_";
            if (cmd.behaviour.size() <= kPrefix.size())
            {
                return true;
            }
            const std::string settingName = cmd.behaviour.substr(kPrefix.size());

            auto forcedIt = sForcedEnvParams.find(settingName);
            if (forcedIt != sForcedEnvParams.end())
            {
                replyOnChannel(std::to_string(forcedIt->second), cmd.param);
                return true;
            }

            // Not currently forced - per spec, @getenv_ answers "the
            // value set by @setenv_ OR BY HAND", so ask newview for the
            // live value. No notify on a miss here (unlike the force
            // side) - matches this spec family's existing convention
            // that an unanswerable query simply gets no reply.
            F32 liveValue = 0.f;
            if (gKRlv.requestGetLiveEnvironmentSetting(settingName, liveValue))
            {
                replyOnChannel(std::to_string(liveValue), cmd.param);
            }
            return true;
        });

    handler.registerIdleTick(pollForcedEnvironment);
}
