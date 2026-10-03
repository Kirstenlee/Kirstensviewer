/**
 * @file krlvhandler.h
 * @brief KRLV central handler - the ONE class the rest of the viewer is
 * meant to ever touch. Owns command dispatch, the active-restriction
 * table, and every public KRlv::canXxx() query. See krlv/README.md for
 * the full design note and the list of every place outside indra/krlv
 * that references this module.
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

#ifndef KRLV_KRLVHANDLER_H
#define KRLV_KRLVHANDLER_H

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "llerror.h"
#include "llsingleton.h"
#include "lluuid.h"
#include "v3dmath.h"
#include "v3math.h"

#include "krlvcommand.h"
#include "krlvobjects.h"
#include "krlvrestriction.h"

// KRLV: one behaviour handler. Returns true if it produced a chat reply or
// otherwise fully handled the command (used later for diagnostics); the
// source object id and owner id are passed through so a handler can record
// a restriction entry or reply on the right channel.
using KRlvBehaviourHandler = std::function<bool(const KRlvCommand&, const LLUUID& sourceId, const LLUUID& ownerId)>;

// KRLV: asks whoever registered the hook (llappviewer.cpp, see
// setFloaterCloseHook()) to close a named floater right now, if it's
// open - WITHOUT krlv/ itself linking llui/llrender/llimage just to
// reach LLFloaterReg (that chain is heavy). Returns true only if an
// instance was found actually VISIBLE and got closed, false if there
// was nothing to do - idle()'s floater-gate enforcement uses that to
// log only when it genuinely had to act.
//
// Deliberately reactive (polled every idle tick, see registerFloaterGate()
// below), not gated at each possible UI entry point (menu, toolbar,
// keyboard shortcut, console, ...). A per-entry-point gate has to be
// re-added every time a new way to open the floater is discovered -
// exactly the "touchpoint whack-a-mole" the architecture is meant to
// avoid (a toolbar button was found to bypass an earlier onOpen()-based
// gate). Polling means one code path catches every entry point,
// existing or future, for free.
using KRlvFloaterCloseRequest = std::function<bool(const std::string& floaterName)>;

// KRLV: same indirection as KRlvFloaterCloseRequest, for one-shot "force"
// commands that need to actively command the avatar (e.g. @setrot) rather
// than just gate/query it - krlv/ cannot reach LLAgent directly (llagent.h
// is newview-only, not a library krlv links), so this is registered once
// from llappviewer.cpp instead.
using KRlvForceRotateRequest = std::function<void(const LLVector3& lookAtDirection)>;

// KRLV: same indirection, for @adjustheight=force (one-shot avatar hover
// offset adjustment). krlv/ parses the 3 raw command parameters
// (targetPelvisToFoot, factor, delta - the last may be 0 if the spec's
// optional 3rd parameter was omitted) but cannot itself read
// gAgentAvatarp->getPelvisToFoot() or touch gSavedPerAccountSettings'
// AvatarHoverOffsetZ (llvoavatarself.h/llviewercontrol.h are newview-
// only), so the hook does both the live measurement read and the actual
// hover-offset write. The exact formula relating the 3 parameters to a
// hover delta is NOT stated in the public RLVa spec text - see
// krlv/README.md's Movement section for the documented, derived formula
// this hook implements (grounded in real platform APIs, not fabricated,
// but not confirmed against RLVa's own internal math either).
using KRlvForceAdjustHeightRequest = std::function<void(F32 targetPelvisToFoot, F32 factor, F32 delta)>;

// KRLV: same indirection, for @setcam_fov=force (one-shot camera FOV
// change) and @getcam_fov (a genuinely LIVE read of the current FOV,
// unlike the other @getcam_* queries below which echo back a stored
// restriction value and need no newview reach-out at all).
using KRlvForceFovRequest = std::function<void(F32 fovRadians)>;
using KRlvGetFovRequest = std::function<F32()>;

// KRLV: same indirection, for @tpto=force's global-coordinate syntax.
// The region-name+local-coordinates syntax reuses this SAME hook once
// resolved to a global position - see KRlvResolveRegionPositionRequest
// below.
using KRlvForceTeleportRequest = std::function<void(const LLVector3d& posGlobal)>;

// KRLV: same indirection, for @tpto=force's region-name+local-coordinates
// syntax ("<region_name>/<X>/<Y>/<Z>") - krlv/ parses the option string
// itself (no newview types needed for that part), but resolving a region
// NAME to a position needs LLWorld/LLViewerRegion, which krlv/ cannot
// reach directly. Returns false if regionName isn't currently known to
// the viewer (only the current region and its already-loaded neighbours
// are searched - a real, honest scope limit, not a bug: there is no
// synchronous grid-wide name lookup, and the async capability-based one
// doesn't fit this module's synchronous command-handling shape - see
// krlv/README.md's Teleportation section). outGlobalPos is
// region->getOriginGlobal() + localPos when found.
using KRlvResolveRegionPositionRequest = std::function<bool(const std::string& regionName, const LLVector3& localPos, LLVector3d& outGlobalPos)>;

// KRLV: same indirection, for @sit:<UUID>=force (find the object by id
// and sit on it), @unsit=force (stand up), and @sitground=force (sit on
// the ground where standing) - all one-shot actions Sitting needs.
using KRlvForceSitRequest = std::function<void(const LLUUID& objectId)>;
using KRlvForceStandRequest = std::function<void()>;
using KRlvForceSitGroundRequest = std::function<void()>;

// KRLV: same indirection, for @getsitid - a genuinely LIVE read of the
// object the avatar is currently sitting on (LLUUID::null if not
// sitting), same shape as KRlvGetFovRequest above.
using KRlvGetSitIdRequest = std::function<LLUUID()>;

// KRLV: same indirection, for @standtp - a genuinely LIVE read of
// whether the avatar is CURRENTLY sitting (return value) plus its
// current global position (always filled regardless of sit state, via
// gAgent.getPositionGlobal()). Polled every idle tick by krlvteleport.cpp
// (see registerIdleTick() below) rather than hooked at LLAgent::standUp()
// itself - standUp() only REQUESTS a stand (AGENT_CONTROL_STAND_UP is
// sent to the simulator, which confirms it asynchronously via a later
// object update), so it is not "the moment the avatar stands" and would
// have meant racing that confirmation. Polling sidesteps the race
// entirely: it doesn't matter exactly when isSitting() flips, only that
// the transition is noticed on whichever frame it actually happens.
using KRlvGetAgentStateRequest = std::function<bool(LLVector3d& outPositionGlobal)>;

// KRLV: same indirection, for @detach[:attachpt]=force,
// @remattach[...]=force (an alias of the same), and @detachme=force -
// an empty attachPointName means "detach the object identified by
// sourceId itself" (@detachme, or the no-point form of @detach=force);
// a non-empty one means "detach everything currently worn at this named
// point".
using KRlvForceDetachRequest = std::function<void(const LLUUID& sourceId, const std::string& attachPointName)>;

// KRLV: same indirection, for @getoutfit[:part] / @getattach[:attachpt] -
// both need a LIVE enumeration of LLWearableType/LLViewerJointAttachment
// names krlv/ cannot reach directly (llappearancemgr.h/llvoavatarself.h
// are newview-only). The hook itself builds and returns the already-
// formatted reply string (a single "0"/"1" for a named part/point, or
// the full canonical-order digit string when partName/pointName is
// empty) - see krlv/README.md's Attachments section.
using KRlvGetOutfitLayersRequest = std::function<std::string(const std::string& partName)>;
using KRlvGetAttachPointsRequest = std::function<std::string(const std::string& pointName)>;

// KRLV: same indirection, for @setgroup:<name>=force - resolves a group
// the agent is already a member of by name (or deactivates entirely for
// the literal name "none") and calls LLGroupActions::activate(), the
// same already-gated function every real "activate group" UI path uses
// - see krlv/README.md's Group section.
using KRlvForceSetGroupRequest = std::function<void(const std::string& groupName)>;

// KRLV: same indirection, for @getgroup - a genuinely LIVE read of the
// currently active group's name ("none" if no group is active), same
// shape as KRlvGetSitIdRequest/KRlvGetFovRequest.
using KRlvGetGroupNameRequest = std::function<std::string()>;

// KRLV: same indirection, for @setenv_<setting>:<value>=force /
// @getenv_<setting> - the legacy per-parameter Windlight force-set
// family (~35 real parameter names: blue_horizon, haze_density, cloud
// colour/position/scroll, sun/moon rotation, daytime, etc). ONE hook
// pair covers the whole family rather than one hook per parameter - all
// name<->LLSettingsSky field translation lives newview-side
// (llappviewer.cpp) since krlv/ cannot link llsettingssky.h/
// llenvironment.h. krlv/ only ever deals in plain name/float pairs; see
// krlvviewercontrol.cpp's file header and krlv/README.md's Viewer
// Control section for the full parameter table, the confirmed legacy-
// haze integration point, the documented approximations (daytime,
// sunglowfocus/sunglowsize), and the genuine gaps (the "i"/intensity
// colour components, "preset") that raise a KRLVStubHit-style notice
// instead of silently no-op'ing.
//
// requestApplyForcedEnvironment() is called BOTH on every real
// @setenv_<setting>=force (one-shot, matches spec) AND, separately,
// every idle tick while @setenv is restricted (krlvviewercontrol.cpp's
// pollForcedEnvironment()) - the self-healing reapplication the user
// asked for, which also plugs a real, independently-confirmed gap:
// LLAgent's teleport-complete handler calls
// LLEnvironment::setSharedEnvironment() directly (gated behind
// "SwitchToSharedEnvAfterTeleport", default true) completely bypassing
// LLWorldEnvSettings::handleEvent() - the chokepoint @setenv=n's own
// restriction gate relies on. The hook always re-derives from the LIVE
// current sky (LLEnvironment::getCurrentSky()) rather than a frozen
// snapshot, so untouched parameters keep tracking reality (day/night
// motion, parcel/region changes) instead of freezing at whatever they
// were when first forced.
using KRlvApplyForcedEnvironmentRequest = std::function<void(const std::map<std::string, F32>& forcedParams)>;

// KRLV: for the half of @getenv_<setting> that isn't already answered
// from krlv/'s own forced-parameter map (see KRlvApplyForcedEnvironmentRequest
// above) - a genuinely LIVE read of a not-yet-forced parameter's current
// value, same shape as KRlvGetFovRequest/KRlvGetSitIdRequest. Returns
// false for a name with no live equivalent (the same confirmed-gap list
// as the force side) - per spec convention, an unanswerable @getenv_
// simply gets no reply, no notification (unlike the force side, where
// the user specifically asked for one - see krlvviewercontrol.cpp).
using KRlvGetLiveEnvironmentSettingRequest = std::function<bool(const std::string& settingName, F32& outValue)>;

// KRLV: same indirection, for the general-purpose "send a plain text
// instant message to a specific avatar" primitive - krlv/ cannot call
// pack_instant_message()/gAgent.sendReliableMessage() directly
// (llinstantmessage.h/llagent.h are newview-only). This is the one
// thing processInboundIM()'s manual-command replies (@version,
// @getblacklist - see KRlvManualImReplyBuilder below) are built on, and
// is deliberately kept generic rather than purpose-built for just those
// two, so a future feature (owner alerts/tamper notifications) can call
// the exact same `gKRlv.requestSendIm(...)` with no new hook - see
// krlv/README.md's "IM subsystem" section.
using KRlvSendImRequest = std::function<void(const LLUUID& toId, const std::string& message)>;

// KRLV: one entry from a one-level folder listing - see
// KRlvListFolderRequest below. `isWorn` is only meaningful when
// `isCategory` is false.
struct KRlvInvEntry
{
    LLUUID id;
    std::string name;
    bool isCategory = false;
    bool isWorn = false;
};

// KRLV: Shared Folders needs a small family of DATA-fetching hooks
// (rather than one hook per command, like every prior category) because
// almost all of this category's real logic - path parsing, depth-first
// search, "." /"~" folder-name filtering, the whole restriction scheme -
// is pure krlv-internal string/tree work once it has these primitives to
// call. See krlv/behaviours/krlvsharedfolders.cpp and krlv/README.md.

// Finds (or creates, mirroring S24's own AOEngine::tick() "#Kirstens"
// pattern) the top-level "#RLV" folder. LLUUID::null if inventory isn't
// usable yet.
using KRlvGetSharedRootRequest = std::function<LLUUID()>;

// One level of a folder's direct children (categories AND items).
using KRlvListFolderRequest = std::function<std::vector<KRlvInvEntry>(const LLUUID& folderId)>;

// id (category or item) -> its OWN name and its PARENT's id - the
// "self name, walk up" primitive path-building needs (repeated calls
// with outParentId as the next `id` collect one path segment per call).
// Returns false if `id` is unknown or has no parent (e.g. it IS the
// inventory root).
using KRlvGetParentRequest = std::function<bool(const LLUUID& id, std::string& outSelfName, LLUUID& outParentId)>;

// Resolves an attach-point name, a clothing-layer (wearable-type) name,
// or a literal UUID string (an in-world attached object's id) to the
// underlying WORN inventory item's id - LLUUID::null if nothing matches
// or nothing is worn there. Used by @getpath/@getpathnew and the whole
// "this" shortcut family.
using KRlvResolveWornRequest = std::function<LLUUID(const std::string& pointOrLayerOrUuid)>;

// True if `itemId` currently lives anywhere under the "#RLV" folder.
using KRlvIsSharedItemRequest = std::function<bool(const LLUUID& itemId)>;

// Batch wear/detach, forwarding to the already-gated
// LLAppearanceMgr::wearItemsOnAvatar()/removeItemsFromAvatar() - see
// krlv/README.md's Attachments section for why those are already safe
// to call directly with a krlv-gathered id list.
using KRlvForceWearBatchRequest = std::function<void(const std::vector<LLUUID>& itemIds, bool replace)>;
using KRlvForceDetachBatchRequest = std::function<void(const std::vector<LLUUID>& itemIds)>;

class KRlvHandler : public LLSingleton<KRlvHandler>
{
    LLSINGLETON(KRlvHandler);
    virtual ~KRlvHandler();

public:
    // KRLV: called once from llappviewer.cpp's startup sequence (the one
    // "integration" touchpoint that brings KRLV to life at all). Wires the
    // idle callback and lets every krlv_register_*_commands() function
    // populate the dispatch table, in a fixed, explicit order - no
    // static-initializer registration, so there is no static-init-order
    // question to reason about later.
    void init();

    // Called once the account is ready (login complete). Clears all per-account state,
    // creates the integrity key if needed, loads the per-account files and starts the tamper checks.
    void onLoggedIn();
    void shutdown();

    // KRLV: the master on/off switch - backed by gSavedSettings
    // "KRLVEnabled" (default off; RLV is opt-in, not opt-out). Every
    // KRlv::canXxx() query checks this ONCE, internally - a touchpoint
    // never has to know this exists.
    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Safeword: drops every active restriction at once and saves the empty table, so
    // nothing returns on re-enable. Does not touch the blacklist or protected debug
    // settings, which are permanent user configuration.
    void clearAllRestrictions();

    // Commands tab: drops every restriction on one behaviour (set by scripts), saved.
    void clearScriptRestrictions(const std::string& behaviour);

    // Command log for the Auto-reply tab: the most recent commands KRLV received, newest
    // last. In memory only, capped, never saved.
    struct KRlvCommandLogEntry
    {
        std::string time;
        LLUUID source;
        LLUUID owner;
        std::string behaviour;
        std::string outcome;
    };
    std::vector<KRlvCommandLogEntry> recentCommands() const;

    // KRLV: registers one behaviour name ("sendim", "detach", ...) to its
    // handler. Called only from the krlv_register_*_commands() functions
    // during init(), never from viewer code outside indra/krlv.
    void registerBehaviour(const std::string& behaviour, KRlvBehaviourHandler handler);

    // KRLV: a handful of RLVa commands glue a dynamic setting name onto a
    // fixed prefix rather than using a ":option" separator - e.g.
    // "@setdebug_RenderDeferred:1=force" parses as behaviour
    // "setdebug_renderdeferred", not "setdebug_" - so no exact-name
    // lookup can ever match every possible setting. Checked only when an
    // exact match fails (see processInboundChat()); prefix is matched
    // against the lowercased, already-parsed behaviour string.
    void registerBehaviourPrefix(const std::string& prefix, KRlvBehaviourHandler handler);

    // KRLV: true if `behaviour` is a registered command name, exact or by prefix, or a manual
    // IM command name. A command line is only handled when every piece passes this check.
    bool isKnownCommandName(const std::string& behaviour) const;

    // KRLV_TOUCHPOINT candidate for the chat-intercept hook
    // (llviewermessage.cpp, process_chat_from_simulator): call for every
    // chat line from CHAT_SOURCE_OBJECT. Returns true if the line was (at
    // least in part) a KRLV command and should NOT be shown as ordinary
    // chat - matches RLVa's own "commands are invisible to the user"
    // behaviour.
    bool processInboundChat(const std::string& message, const LLUUID& sourceId, const LLUUID& ownerId);

    // KRLV: a SMALL, separate family from the chat-form commands above -
    // the spec's own "manual" commands (@version, @getblacklist) that
    // take no parameters at all (no channel, no option). Per spec these
    // only work sent in IM from an avatar, answered silently via IM
    // ("neither the message nor the answer appears in the user's IM
    // window") - see processInboundIM() below. S24 ALSO lets the user
    // invoke the same bare command from their OWN typed chat for
    // convenience (the user's own explicit request) - see
    // processSelfChatCommand() below, which shares this same
    // registration table but replies locally instead of over IM, since
    // there is no "stay hidden from yourself" requirement to honour
    // there. Each category registers its own bare command name against
    // a reply-building callback (zero arguments - these commands carry
    // no parameters by definition); `replyBuilder` returning an empty
    // string means "handled, but nothing to send back" (reserved for a
    // future command that acts without replying). See
    // krlvversion.cpp/krlvblacklist.cpp for the two real registrations
    // and krlv/README.md's "IM subsystem" section.
    using KRlvManualImReplyBuilder = std::function<std::string()>;
    void registerManualImCommand(const std::string& command, KRlvManualImReplyBuilder replyBuilder);

    // KRLV_TOUCHPOINT candidate: call from newview's inbound-IM
    // processing (llimprocessing.cpp's `IM_NOTHING_SPECIAL` case - "p2p
    // IM", already by construction never an object) for every incoming
    // IM, BEFORE any @recvim-family gating (these commands work
    // regardless of @recvim, matching the chat-form equivalents' own
    // universal availability regardless of @sendchat/@recvchat). Returns
    // true if `message` was exactly one of the registered manual IM
    // commands (case-insensitively, leading/trailing whitespace
    // trimmed, no trailing content of any kind) and was fully handled -
    // the caller should then suppress the IM from reaching chat
    // history/notification entirely, matching the spec's stealth
    // requirement. A reply, if the builder returned one, is already sent
    // via requestSendIm() before this returns.
    bool processInboundIM(const std::string& message, const LLUUID& fromId);

    // KRLV_TOUCHPOINT candidate: call from
    // LLFloaterIMNearbyChat::sendChat() for the user's OWN typed chat
    // text (channel 0 only), BEFORE it's ever sent to the simulator -
    // same shape as the existing gesture-trigger check right next to it
    // in that function, and the same reason: answering locally here,
    // before any network round-trip, is both correct (these commands
    // are a local viewer capability query, not something that needs a
    // server round-trip at all) and avoids the chat-echo latency a
    // round-trip would add. Same exact-match rules as processInboundIM()
    // (shares the same mManualImCommands table), but does NOT send
    // anything over IM - the matched reply (if any) is returned in
    // `outReply` for the caller to echo locally (e.g. a local nearby-
    // chat system message), and the original text should never reach
    // sendChatFromViewer() at all if this returns true, matching every
    // other KRLV command's "invisible to the user" convention.
    bool processSelfChatCommand(const std::string& message, std::string& outReply);

    // KRLV: per-frame housekeeping (registered with gIdleCallbacks in
    // init()) - continuous enforcement (camera clamps, timed restriction
    // expiry) lives here so it never needs its own viewer-side touchpoint.
    static void idle(void* userData);

    // KRLV: thin entry point for the S24 Console's "krlv" command
    // (kvdebugconsole.cpp) - all real logic stays in this module, the
    // console just prints what this returns.
    std::string handleConsoleCommand(const std::string& args);

    size_t registeredBehaviourCount() const { return mHandlers.size(); }
    size_t activeRestrictionCount() const;

    // KRLV: the generic restriction store every real behaviour handler
    // uses instead of hand-rolling its own bool/state - one source of
    // truth, persisted (see krlv/core/krlvpersist.h) after every
    // mutation. `option` is whatever followed ":" on the command that
    // imposed it, if any (most behaviours don't use it).
    bool isRestricted(const std::string& behaviour) const;
    void addRestriction(const std::string& behaviour, const LLUUID& sourceId, const std::string& option = std::string());
    void removeRestriction(const std::string& behaviour, const LLUUID& sourceId);

    // KRLV: every active entry's option string for `behaviour`, in no
    // particular order - empty if none active. For a numeric-parameter
    // restriction (camzoommax, camdistmax, ...) a category computes its
    // own documented multi-source reduction (min/max - the RLVa spec
    // states this per-command, it is not a generic rule) from this list;
    // see krlv/behaviours/krlvcamera.cpp for the first real user.
    std::vector<std::string> getRestrictionOptions(const std::string& behaviour) const;

    // KRLV: true if `sourceId` specifically holds an active entry on
    // `behaviour` - unlike isRestricted() (which only asks "is anyone
    // holding this at all"). For an exception list (e.g. @tplure:<UUID>=add),
    // `sourceId` is the EXEMPTED avatar: the exception is stored under
    // exceptionKey(behaviour, avatar), so any imposing object counts.
    // See krlv/behaviours/krlvteleport.cpp for the first real user.
    bool hasRestrictionFrom(const std::string& behaviour, const LLUUID& sourceId) const;

    // KRLV: key for a per-avatar exception, "<behaviour>:<avatar-uuid>". The imposing object is
    // the entry's source, so two objects exempting the same avatar do not collide. For such keys,
    // hasRestrictionFrom(behaviour, avatar) answers true when any imposing object holds the exception.
    static std::string exceptionKey(const std::string& behaviour, const LLUUID& avatar)
    {
        return behaviour + ":" + avatar.asString();
    }

    // KRLV: every active (behaviour, option) pair currently held by
    // `sourceId` - LLUUID::null means "every source" (the wildcard
    // @getstatusall needs, vs. @getstatus's own-source-only form). Used
    // by krlvmisc.cpp's @getstatus/@getstatusall - see krlv/README.md's
    // Miscellaneous section for the real limitation this has (a
    // restriction whose entry.sourceObjectId holds a TARGET id rather
    // than the imposing object's own id - e.g. @editobj:<UUID>=n,
    // @touchthis:<UUID>=rem/add - can't be told apart from this method's
    // point of view, so @getstatus/@clear only see it under the
    // TARGET's id, never the imposing object's).
    std::vector<std::pair<std::string, std::string>> getActiveRules(const LLUUID& sourceId) const;

    // KRLV: removes every entry, across ALL restriction keys, whose
    // entry.sourceObjectId == `sourceId` and (filter.empty() ||
    // behaviour.find(filter) != npos) - @clear's real implementation.
    // Same target-vs-imposer limitation as getActiveRules() above.
    // Returns the (behaviour, option) pairs actually removed, so the
    // caller (krlvmisc.cpp) doesn't need a second pass to know what
    // changed. Fires @notify subscribers WITHOUT the "=y" suffix a
    // plain removeRestriction() would add - the spec's own stated
    // "@clear will not add an equal sign" rule.
    std::vector<std::pair<std::string, std::string>> clearRestrictionsFrom(const std::string& filter, const LLUUID& sourceId);

    // KRLV: the user-configured, PERMANENT blacklist - command NAMES
    // the viewer refuses to process from ANY object, regardless of any
    // active restriction state. Distinct from the restriction table
    // above (per-object, moment-to-moment) - this is static user-side
    // configuration, checked once in processInboundChat() before any
    // handler runs for a given parsed command, and persisted separately
    // (krlv_blacklist.dat, see krlvpersist.h). Configured ONLY via the
    // S24 Console ("krlv blacklist add/remove/list") - never by a chat
    // command from an object, which would defeat the entire point of a
    // user-side blacklist. `behaviour` is matched exactly against
    // KRlvCommand::behaviour (already lowercased at parse time).
    bool isBlacklisted(const std::string& behaviour) const;
    void addToBlacklist(const std::string& behaviour);
    void removeFromBlacklist(const std::string& behaviour);
    std::vector<std::string> getBlacklist() const; // sorted, for @getblacklist's reply and console listing

    // Object whitelist/blacklist (krlvobjects.h). Every change is saved immediately.
    KRlv::KRlvObjectLists getObjectLists() const;
    void setObjectMode(KRlv::KRlvObjectMode mode);
    void addObjectEntry(bool whitelist, const LLUUID& object, const LLUUID& owner);
    void removeObjectEntry(bool whitelist, const LLUUID& object, const LLUUID& owner);

    // KRLV: the user-configured whitelist of Advanced > Debug Settings
    // floater rows @setdebug=n protects from edits - infrastructure for
    // a restriction the real RLVa spec describes as locking a small,
    // undocumented whitelist (contents never published), so this viewer
    // builds and persists its own (krlv_protected_debug.dat, see
    // krlvpersist.h) instead of guessing at RLVa's internal one. Same
    // shape as the blacklist above, but NOT case-folded -
    // gSavedSettings control lookups are case-sensitive, so `settingName`
    // is matched exactly as given. Configured ONLY via the S24 Console
    // for now ("krlv protectdebug add/remove/list") - same bridge-until-
    // a-floater-exists shape the blacklist already uses; a future
    // user-facing settings picker is meant to call these same four
    // methods instead of growing its own storage. See
    // krlvviewercontrol.cpp's file header and krlv/README.md's Viewer
    // Control section.
    bool isDebugSettingProtected(const std::string& settingName) const;
    void addProtectedDebugSetting(const std::string& settingName);
    void removeProtectedDebugSetting(const std::string& settingName);
    std::vector<std::string> getProtectedDebugSettings() const; // sorted

    // KRLV: true if ANY restriction key starting with `prefix` currently
    // has at least one active entry - for the dynamically-keyed per-
    // point/per-part/per-folder restriction families (Attachments'
    // "attach_point:<name>", Shared Folders' "detachthis:<folderId>", ...)
    // where a caller needs "is anything in this whole family active"
    // without enumerating every possible concrete key up front. First
    // real user: the rez_attachment() NULL-attachment-pointer case (see
    // krlv/README.md's Attachments section) - when the target attach
    // point can't be determined client-side, this lets the gate fail
    // safe (block) rather than silently bypassing a per-point lock.
    bool hasAnyRestrictionWithPrefix(const std::string& prefix) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to init(). See KRlvFloaterCloseRequest
    // above for why this indirection exists instead of a direct
    // LLFloaterReg call from within krlv/.
    void setFloaterCloseHook(KRlvFloaterCloseRequest hook);
    bool requestFloaterClose(const std::string& floaterName) const;

    // KRLV: registers `behaviour` as gating `floaterName` - checked every
    // idle tick (see idle()), not at each possible UI entry point. Call
    // from a category's krlv_register_*_commands() function, same as
    // registerBehaviour(). One behaviour may gate at most one floater per
    // call, but nothing stops registering several (@showloc gates
    // "world_map" in addition to its own bespoke handler logic).
    void registerFloaterGate(const std::string& behaviour, const std::string& floaterName);

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setFloaterCloseHook(). See
    // KRlvForceRotateRequest above.
    void setForceRotateHook(KRlvForceRotateRequest hook);
    void requestForceRotate(const LLVector3& lookAtDirection) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setForceRotateHook(). See
    // KRlvForceAdjustHeightRequest above.
    void setForceAdjustHeightHook(KRlvForceAdjustHeightRequest hook);
    void requestForceAdjustHeight(F32 targetPelvisToFoot, F32 factor, F32 delta) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setForceRotateHook(). See
    // KRlvForceFovRequest/KRlvGetFovRequest above.
    void setForceFovHook(KRlvForceFovRequest hook);
    void requestForceFov(F32 fovRadians) const;
    void setGetFovHook(KRlvGetFovRequest hook);
    F32 requestGetFov() const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setForceFovHook(). See
    // KRlvForceTeleportRequest above.
    void setForceTeleportHook(KRlvForceTeleportRequest hook);
    void requestForceTeleport(const LLVector3d& posGlobal) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setForceTeleportHook(). See
    // KRlvResolveRegionPositionRequest above.
    void setResolveRegionPositionHook(KRlvResolveRegionPositionRequest hook);
    bool requestResolveRegionPosition(const std::string& regionName, const LLVector3& localPos, LLVector3d& outGlobalPos) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setForceTeleportHook(). See
    // KRlvForceSitRequest/KRlvForceStandRequest/KRlvForceSitGroundRequest/
    // KRlvGetSitIdRequest above.
    void setForceSitHook(KRlvForceSitRequest hook);
    void requestForceSit(const LLUUID& objectId) const;
    void setForceStandHook(KRlvForceStandRequest hook);
    void requestForceStand() const;
    void setForceSitGroundHook(KRlvForceSitGroundRequest hook);
    void requestForceSitGround() const;
    void setGetSitIdHook(KRlvGetSitIdRequest hook);
    LLUUID requestGetSitId() const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setGetSitIdHook(). See
    // KRlvGetAgentStateRequest above.
    void setGetAgentStateHook(KRlvGetAgentStateRequest hook);
    bool requestGetAgentState(LLVector3d& outPositionGlobal) const;

    // KRLV: registers a callback invoked from idle() every tick (after the
    // isEnabled() check above, so a callback never needs to check it
    // itself), for restriction logic that needs continuous per-frame state
    // observation rather than a one-shot command handler - e.g. @standtp's
    // sit/stand transition tracking (krlvteleport.cpp). Call from a
    // category's krlv_register_*_commands() function, same as
    // registerBehaviour()/registerFloaterGate(). Deliberately generic
    // (krlvhandler.cpp knows nothing about what any given callback does) -
    // keeps category-specific per-frame logic out of the core handler, the
    // same separation registerBehaviour() already maintains for commands.
    void registerIdleTick(std::function<void()> callback);

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setGetSitIdHook(). See
    // KRlvForceDetachRequest above.
    void setForceDetachHook(KRlvForceDetachRequest hook);
    void requestForceDetach(const LLUUID& sourceId, const std::string& attachPointName) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setForceDetachHook(). See
    // KRlvGetOutfitLayersRequest/KRlvGetAttachPointsRequest above.
    void setGetOutfitLayersHook(KRlvGetOutfitLayersRequest hook);
    std::string requestGetOutfitLayers(const std::string& partName) const;
    void setGetAttachPointsHook(KRlvGetAttachPointsRequest hook);
    std::string requestGetAttachPoints(const std::string& pointName) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setGetAttachPointsHook(). See
    // KRlvForceSetGroupRequest/KRlvGetGroupNameRequest above.
    void setForceSetGroupHook(KRlvForceSetGroupRequest hook);
    void requestForceSetGroup(const std::string& groupName) const;
    void setGetGroupNameHook(KRlvGetGroupNameRequest hook);
    std::string requestGetGroupName() const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setGetAttachPointsHook(). See the
    // Shared Folders hook family above.
    void setGetSharedRootHook(KRlvGetSharedRootRequest hook);
    LLUUID requestGetSharedRoot() const;
    void setListFolderHook(KRlvListFolderRequest hook);
    std::vector<KRlvInvEntry> requestListFolder(const LLUUID& folderId) const;
    void setGetParentHook(KRlvGetParentRequest hook);
    bool requestGetParent(const LLUUID& id, std::string& outSelfName, LLUUID& outParentId) const;
    void setResolveWornHook(KRlvResolveWornRequest hook);
    LLUUID requestResolveWorn(const std::string& pointOrLayerOrUuid) const;
    void setIsSharedItemHook(KRlvIsSharedItemRequest hook);
    bool requestIsSharedItem(const LLUUID& itemId) const;
    void setForceWearBatchHook(KRlvForceWearBatchRequest hook);
    void requestForceWearBatch(const std::vector<LLUUID>& itemIds, bool replace) const;
    void setForceDetachBatchHook(KRlvForceDetachBatchRequest hook);
    void requestForceDetachBatch(const std::vector<LLUUID>& itemIds) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setGetGroupNameHook(). See
    // KRlvApplyForcedEnvironmentRequest/KRlvGetLiveEnvironmentSettingRequest
    // above.
    void setApplyForcedEnvironmentHook(KRlvApplyForcedEnvironmentRequest hook);
    void requestApplyForcedEnvironment(const std::map<std::string, F32>& forcedParams) const;
    void setGetLiveEnvironmentSettingHook(KRlvGetLiveEnvironmentSettingRequest hook);
    bool requestGetLiveEnvironmentSetting(const std::string& settingName, F32& outValue) const;

    // KRLV_TOUCHPOINT candidate: called once from llappviewer.cpp's
    // startup sequence, right next to setGetLiveEnvironmentSettingHook().
    // See KRlvSendImRequest above.
    void setSendImHook(KRlvSendImRequest hook);
    void requestSendIm(const LLUUID& toId, const std::string& message) const;

private:
    // KRLV: the "krlv blacklist [add|remove <name>]" sub-command's own
    // logic, split out of handleConsoleCommand() purely for readability
    // - `rest` is everything after the literal word "blacklist".
    std::string handleBlacklistConsoleCommand(const std::string& rest);

    // KRLV: the "krlv protectdebug [add|remove <name>]" sub-command's
    // own logic, same split-out-for-readability shape as
    // handleBlacklistConsoleCommand() above - `rest` is everything after
    // the literal word "protectdebug". Unlike that one, names are NOT
    // lowercased (case-sensitive gSavedSettings control names).
    std::string handleProtectedDebugConsoleCommand(const std::string& rest);

    // KRLV: the exact-match/trim/lowercase/strip-"@" logic shared by
    // processInboundIM() and processSelfChatCommand() - both commands'
    // invocation RULES are identical (bare text, no parameters, exact
    // match against mManualImCommands), only the REPLY DELIVERY differs
    // (IM send vs local echo, decided by each public caller). Returns
    // true and fills `outReply` (possibly empty, meaning "handled, no
    // reply") on a match; false (outReply untouched) otherwise.
    bool matchManualImCommand(const std::string& message, std::string& outReply) const;

    // KRLV: @notify's reply shape - see krlvmisc.cpp's file header.
    enum class NotifyKind { Added, Removed, Cleared };

    // KRLV: fires every active "notify" subscription (see krlvmisc.cpp)
    // whose word filter matches `behaviour`, replying "/behaviour[:option]"
    // plus "=n"/"=y" (Added/Removed) or nothing at all (Cleared - the
    // spec's own "@clear will not add an equal sign" rule). Called from
    // addRestriction()/removeRestriction()/clearRestrictionsFrom()
    // themselves, not by any touchpoint.
    void notifyRestrictionChange(const std::string& behaviour, const std::string& option, NotifyKind kind);
    void loadAccountState();

    std::unordered_map<std::string, KRlvBehaviourHandler> mHandlers;
    std::vector<std::pair<std::string, KRlvBehaviourHandler>> mPrefixHandlers;
    KRlvRestrictionTable mRestrictions;
    std::unordered_set<std::string> mBlacklist;
    KRlv::KRlvObjectLists mObjectLists;
    std::vector<KRlvCommandLogEntry> mCommandLog;
    void logCommand(const KRlvCommand& cmd, const LLUUID& source, const LLUUID& owner, const std::string& outcome);
    std::unordered_set<std::string> mProtectedDebugSettings;
    KRlvFloaterCloseRequest mFloaterCloseHook;
    std::vector<std::pair<std::string, std::string>> mFloaterGates; // behaviour -> floater name
    KRlvForceRotateRequest mForceRotateHook;
    KRlvForceAdjustHeightRequest mForceAdjustHeightHook;
    KRlvForceFovRequest mForceFovHook;
    KRlvGetFovRequest mGetFovHook;
    KRlvForceTeleportRequest mForceTeleportHook;
    KRlvResolveRegionPositionRequest mResolveRegionPositionHook;
    KRlvForceSitRequest mForceSitHook;
    KRlvForceStandRequest mForceStandHook;
    KRlvForceSitGroundRequest mForceSitGroundHook;
    KRlvGetSitIdRequest mGetSitIdHook;
    KRlvGetAgentStateRequest mGetAgentStateHook;
    std::vector<std::function<void()>> mIdleTickCallbacks;
    KRlvForceDetachRequest mForceDetachHook;
    KRlvGetOutfitLayersRequest mGetOutfitLayersHook;
    KRlvGetAttachPointsRequest mGetAttachPointsHook;
    KRlvForceSetGroupRequest mForceSetGroupHook;
    KRlvGetGroupNameRequest mGetGroupNameHook;
    KRlvGetSharedRootRequest mGetSharedRootHook;
    KRlvListFolderRequest mListFolderHook;
    KRlvGetParentRequest mGetParentHook;
    KRlvResolveWornRequest mResolveWornHook;
    KRlvIsSharedItemRequest mIsSharedItemHook;
    KRlvForceWearBatchRequest mForceWearBatchHook;
    KRlvForceDetachBatchRequest mForceDetachBatchHook;
    KRlvApplyForcedEnvironmentRequest mApplyForcedEnvironmentHook;
    KRlvGetLiveEnvironmentSettingRequest mGetLiveEnvironmentSettingHook;
    KRlvSendImRequest mSendImHook;
    std::unordered_map<std::string, KRlvManualImReplyBuilder> mManualImCommands;
    bool mSharedRootEnsured = false;
    F64 mSharedRootRetryAt = 0.0; // LLTimer seconds; no shared-root request before this time
};

#define gKRlv (KRlvHandler::instance())

#endif // KRLV_KRLVHANDLER_H
