/**
 * @file krlvhandler.cpp
 * @brief KRLV central handler implementation.
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

#include "krlvhandler.h"
#include "krlvtamper.h"
#include <ctime>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "llerror.h"
#include "llcallbacklist.h"
#include "llchat.h"
#include "llcontrol.h"
#include "llsd.h"

#include "krlvbehaviours.h"
#include "krlvpersist.h"
#include "krlvintegrity.h"
#include "lltimer.h"

namespace
{
    // Wait between requests for the "#RLV" folder while it is not yet found.
    constexpr F64 KRLV_SHARED_ROOT_RETRY_SECONDS = 30.0;
}

extern LLControlGroup gSavedSettings;

// KRLV: same plain external-linkage free function krlvversion.cpp
// forward-declares locally - see that file's comment on why (no header
// exports it, calling it doesn't need one).
void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

KRlvHandler::KRlvHandler()
{
}

KRlvHandler::~KRlvHandler()
{
}

void KRlvHandler::onLoggedIn()
{
    // Per-account state must be cleared first: this is also the account-switch path.
    mRestrictions.clear();
    mBlacklist.clear();
    mObjectLists = KRlv::KRlvObjectLists();
    mProtectedDebugSettings.clear();
    mCommandLog.clear();
    mSharedRootEnsured = false;
    mSharedRootRetryAt = 0.0;

    // Creates the integrity key if it does not exist yet, so it is saved with settings.
    KRlv::ensureIntegrityKey();

    loadAccountState();
    KRlv::startTamperChecks();
}

void KRlvHandler::loadAccountState()
{
    // Per-account files: LL_PATH_PER_SL_ACCOUNT is only valid after login.
    KRlv::loadRestrictions(mRestrictions);
    KRlv::loadBlacklist(mBlacklist);
    KRlv::loadObjectLists(mObjectLists);
    KRlv::loadProtectedDebugSettings(mProtectedDebugSettings);
}

void KRlvHandler::init()
{
    // KRLV: each category registers itself explicitly, in a fixed order -
    // deliberately NOT static-initializer self-registration, so there is
    // no static-initialization-order question to ever debug. See
    // krlv/README.md for the command list.
    krlv_register_version_commands(*this);
    krlv_register_blacklist_commands(*this);
    krlv_register_misc_commands(*this);
    krlv_register_movement_commands(*this);
    krlv_register_camera_commands(*this);
    krlv_register_chat_commands(*this);
    krlv_register_teleport_commands(*this);
    krlv_register_inventory_commands(*this);
    krlv_register_sitting_commands(*this);
    krlv_register_attachment_commands(*this);
    krlv_register_shared_folder_commands(*this);
    krlv_register_touch_commands(*this);
    krlv_register_location_commands(*this);
    krlv_register_name_commands(*this);
    krlv_register_group_commands(*this);
    krlv_register_viewer_control_commands(*this);
    krlv_register_unofficial_commands(*this);

    gIdleCallbacks.addFunction(idle, this);
}

void KRlvHandler::shutdown()
{
    // KRLV: one last save so an ordinary quit is never the reason a
    // restriction fails to survive to the next login - addRestriction()/
    // removeRestriction() already save on every mutation, this just
    // covers the case where nothing changed between them and now.
    KRlv::saveRestrictions(mRestrictions);
    gIdleCallbacks.deleteFunction(idle, this);
}

void KRlvHandler::logCommand(const KRlvCommand& cmd, const LLUUID& source, const LLUUID& owner, const std::string& outcome)
{
    KRlvCommandLogEntry entry;
    const std::time_t now = std::time(nullptr);
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));
    entry.time = stamp;
    entry.source = source;
    entry.owner = owner;
    entry.behaviour = cmd.behaviour;
    entry.outcome = outcome;

    mCommandLog.push_back(entry);
    if (mCommandLog.size() > 10)
    {
        mCommandLog.erase(mCommandLog.begin());
    }
}

std::vector<KRlvHandler::KRlvCommandLogEntry> KRlvHandler::recentCommands() const
{
    return mCommandLog;
}

void KRlvHandler::clearScriptRestrictions(const std::string& behaviour)
{
    if (mRestrictions.erase(behaviour) > 0)
    {
        KRlv::saveRestrictions(mRestrictions);
    }
}

void KRlvHandler::clearAllRestrictions()
{
    mRestrictions.clear();
    KRlv::saveRestrictions(mRestrictions);
}

bool KRlvHandler::isEnabled() const
{
    // Cached control reference: no string lookup per call (idle() calls this often).
    static LLCachedControl<bool> sEnabled(gSavedSettings, "KRLVEnabled", false);
    return sEnabled;
}

void KRlvHandler::setEnabled(bool enabled)
{
    gSavedSettings.setBOOL("KRLVEnabled", enabled);
}

void KRlvHandler::registerBehaviour(const std::string& behaviour, KRlvBehaviourHandler handler)
{
    mHandlers[behaviour] = std::move(handler);
}

void KRlvHandler::registerBehaviourPrefix(const std::string& prefix, KRlvBehaviourHandler handler)
{
    mPrefixHandlers.emplace_back(prefix, std::move(handler));
}

bool KRlvHandler::isKnownCommandName(const std::string& behaviour) const
{
    if (mHandlers.count(behaviour) > 0 || mManualImCommands.count(behaviour) > 0)
    {
        return true;
    }
    for (const auto& [prefix, handler] : mPrefixHandlers)
    {
        if (behaviour.rfind(prefix, 0) == 0)
        {
            return true;
        }
    }
    return false;
}

bool KRlvHandler::processInboundChat(const std::string& message, const LLUUID& sourceId, const LLUUID& ownerId)
{
    if (!isEnabled())
    {
        return false;
    }

    // A command line needs every comma-separated piece to be a command with a known name.
    // Anything else (e.g. "Welcome, @everyone") is ordinary chat and is not handled here.
    std::vector<KRlvCommand> commands = KRlv::parseMessage(message);
    if (commands.empty())
    {
        return false;
    }
    for (const KRlvCommand& cmd : commands)
    {
        if (!isKnownCommandName(cmd.behaviour))
        {
            return false;
        }
    }

    // Object lists decide per source object, so one check covers the whole message.
    // Refused the same way as a blacklisted command: logged, then invisible in chat.
    std::string objectReason;
    if (!KRlv::objectAllowed(mObjectLists, sourceId, ownerId, objectReason))
    {
        for (const KRlvCommand& cmd : commands)
        {
            logCommand(cmd, sourceId, ownerId, objectReason);
        }
        LL_DEBUGS("KRLV") << "Object refused (" << objectReason << "): " << sourceId << LL_ENDL;
        return true;
    }

    for (const KRlvCommand& cmd : commands)
    {
        logCommand(cmd, sourceId, ownerId, isBlacklisted(cmd.behaviour) ? "refused: blacklisted" : "received");
        if (isBlacklisted(cmd.behaviour))
        {
            // KRLV Blacklist: a user-configured, PERMANENT refusal to
            // process this command name from ANY object - checked
            // before dispatch even looks it up, so a blacklisted
            // command never reaches its handler regardless of what
            // restriction state is otherwise active. Still counts as
            // "handled" (invisible in chat), matching how every other
            // recognized-but-inert command behaves.
            LL_DEBUGS("KRLV") << "Blacklisted command, refused: @" << cmd.behaviour << LL_ENDL;
            continue;
        }

        auto it = mHandlers.find(cmd.behaviour);
        if (it != mHandlers.end())
        {
            it->second(cmd, sourceId, ownerId);
            continue;
        }

        // A few commands (setdebug_<setting>, getenv_<setting>, ...)
        // glue a dynamic name onto a fixed prefix - no exact match can
        // ever cover every possible setting, so check registered
        // prefixes before giving up.
        bool matchedPrefix = false;
        for (const auto& [prefix, handler] : mPrefixHandlers)
        {
            if (cmd.behaviour.rfind(prefix, 0) == 0)
            {
                handler(cmd, sourceId, ownerId);
                matchedPrefix = true;
                break;
            }
        }

        if (!matchedPrefix)
        {
            // A genuinely unrecognized command - not in the RLVa spec at
            // all (typo, or a future/unofficial extension KRLV doesn't
            // know about yet). Swallowed silently in chat, same as
            // RLVa's own "ignore what you don't understand" behaviour.
            // Still logged (not chat-visible) so "no reply" during
            // testing can be told apart from "dispatch never ran at
            // all" by checking the log file.
            LL_DEBUGS("KRLV") << "Unrecognized command, ignored: @" << cmd.behaviour << LL_ENDL;
        }
    }

    return true;
}

void KRlvHandler::registerManualImCommand(const std::string& command, KRlvManualImReplyBuilder replyBuilder)
{
    mManualImCommands[command] = std::move(replyBuilder);
}

bool KRlvHandler::matchManualImCommand(const std::string& message, std::string& outReply) const
{
    // These commands take no parameters at all (no channel, no option) -
    // an exact match on the trimmed, lowercased, leading-"@"-stripped
    // text only, never a prefix/substring match. "@version:foo" or
    // "@version=5" is NOT this command - it just isn't one of the
    // manual commands at all, and falls through to normal handling same
    // as any other ordinary message.
    std::string trimmed = message;
    size_t start = trimmed.find_first_not_of(" \t");
    size_t end = trimmed.find_last_not_of(" \t");
    trimmed = (start == std::string::npos) ? std::string() : trimmed.substr(start, end - start + 1);

    if (trimmed.empty() || trimmed[0] != '@')
    {
        return false;
    }
    trimmed = trimmed.substr(1);
    std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), ::tolower);

    auto it = mManualImCommands.find(trimmed);
    if (it == mManualImCommands.end())
    {
        return false;
    }

    outReply = it->second();
    return true;
}

bool KRlvHandler::processInboundIM(const std::string& message, const LLUUID& fromId)
{
    if (!isEnabled())
    {
        return false;
    }

    std::string reply;
    if (!matchManualImCommand(message, reply))
    {
        return false;
    }

    if (!reply.empty())
    {
        requestSendIm(fromId, reply);
    }
    return true;
}

bool KRlvHandler::processSelfChatCommand(const std::string& message, std::string& outReply)
{
    if (!isEnabled())
    {
        return false;
    }

    return matchManualImCommand(message, outReply);
}

// static
void KRlvHandler::idle(void* userData)
{
    // KRLV: continuous per-frame enforcement - camera clamps and timed
    // restriction expiry land here in a later stage; floater-gate
    // enforcement (registerFloaterGate()) is the first thing to use it.
    // Deliberately reactive rather than preventive: this is what lets
    // krlv/ know nothing about the many possible ways a floater can be
    // shown (menu, toolbar, keyboard shortcut, ...) - it just keeps
    // checking "should this be closed right now?" every frame, so any
    // entry point that slips past is caught within one frame regardless.
    KRlvHandler* self = static_cast<KRlvHandler*>(userData);
    if (!self || !self->isEnabled())
    {
        return;
    }

    // KRLV: makes sure "#RLV" (Shared Folders' root folder) exists the
    // moment KRLV is enabled, rather than only lazily on the first
    // command that needs it - retried every frame (cheap: one bool
    // check once it succeeds) rather than once at startup, since
    // inventory may not be usable yet this early - mirrors this
    // viewer's own AOEngine::tick() "#Kirstens" pattern, which is
    // robust the same way by simply being called every frame. See
    // krlv/README.md's Shared Folders section.
    // The hook may only request creation of the folder: the viewer creates it asynchronously.
    // After a request that did not yet return an id, wait before asking again, so one folder
    // is not created per frame. The wait is 30 seconds or until the folder is found.
    if (!self->mSharedRootEnsured && LLTimer::getElapsedSeconds() >= self->mSharedRootRetryAt)
    {
        if (self->requestGetSharedRoot().notNull())
        {
            self->mSharedRootEnsured = true;
        }
        else
        {
            self->mSharedRootRetryAt = LLTimer::getElapsedSeconds() + KRLV_SHARED_ROOT_RETRY_SECONDS;
        }
    }

    for (const auto& [behaviour, floaterName] : self->mFloaterGates)
    {
        if (self->isRestricted(behaviour) && self->requestFloaterClose(floaterName))
        {
            // requestFloaterClose() only returns true if it found the
            // floater genuinely VISIBLE and closed it - something drew
            // it despite the restriction, worth a loud, explicit log
            // line rather than silently swallowing it every frame.
            LL_DEBUGS("KRLV") << "Closed \"" << floaterName << "\" - it was open/drawn while @"
                << behaviour << " is restricted (potential restriction bypass via some other UI path)."
                << LL_ENDL;
        }
    }

    // KRLV: generic per-category idle-tick callbacks - see
    // registerIdleTick()'s own comment. Copies the vector's size once
    // rather than a range-for, purely defensive: a callback registered
    // during krlv_register_*_commands() never mutates mIdleTickCallbacks
    // itself, but there is no reason to rely on that staying true forever.
    for (size_t i = 0; i < self->mIdleTickCallbacks.size(); ++i)
    {
        self->mIdleTickCallbacks[i]();
    }
}

size_t KRlvHandler::activeRestrictionCount() const
{
    size_t total = 0;
    for (const auto& [behaviour, entries] : mRestrictions)
    {
        total += entries.size();
    }
    return total;
}

bool KRlvHandler::isRestricted(const std::string& behaviour) const
{
    // KRLV_BUGFIX: every real touchpoint in the viewer calls this (or
    // hasRestrictionFrom()/getRestrictionOptions() below) to ask "is
    // this active right now" - NONE of them separately check
    // isEnabled() themselves (by design - a touchpoint is never meant to
    // know the master switch exists). This function is therefore the
    // ONLY place that guarantee can be enforced. Before this fix, a
    // restriction that was ever imposed stayed enforced FOREVER - even
    // across KRLVEnabled being turned off - because mRestrictions is
    // loaded from disk in init() and never cleared by setEnabled(false).
    if (!isEnabled())
    {
        return false;
    }
    auto it = mRestrictions.find(behaviour);
    return it != mRestrictions.end() && !it->second.empty();
}

bool KRlvHandler::hasRestrictionFrom(const std::string& behaviour, const LLUUID& sourceId) const
{
    if (!isEnabled())
    {
        return false;
    }
    // Per-avatar exception keys (see exceptionKey()): any imposing object counts.
    if (const auto keyed = mRestrictions.find(exceptionKey(behaviour, sourceId));
        keyed != mRestrictions.end() && !keyed->second.empty())
    {
        return true;
    }
    auto it = mRestrictions.find(behaviour);
    if (it == mRestrictions.end())
    {
        return false;
    }
    for (const KRlvRestrictionEntry& entry : it->second)
    {
        if (entry.sourceObjectId == sourceId)
        {
            return true;
        }
    }
    return false;
}

bool KRlvHandler::hasAnyRestrictionWithPrefix(const std::string& prefix) const
{
    if (!isEnabled())
    {
        return false;
    }
    for (const auto& entry : mRestrictions)
    {
        if (!entry.second.empty() && entry.first.rfind(prefix, 0) == 0)
        {
            return true;
        }
    }
    return false;
}

bool KRlvHandler::isBlacklisted(const std::string& behaviour) const
{
    // Same master-switch guard every other restriction-reading query
    // enforces at the read side, not just the parsing side - see
    // feedback_krlv_master_switch_enforcement_point. In practice
    // processInboundChat() already early-returns before this could be
    // reached while disabled, but the guarantee belongs here too.
    if (!isEnabled())
    {
        return false;
    }
    return mBlacklist.find(behaviour) != mBlacklist.end();
}

void KRlvHandler::addToBlacklist(const std::string& behaviour)
{
    if (mBlacklist.insert(behaviour).second)
    {
        KRlv::saveBlacklist(mBlacklist);
    }
}

void KRlvHandler::removeFromBlacklist(const std::string& behaviour)
{
    if (mBlacklist.erase(behaviour) > 0)
    {
        KRlv::saveBlacklist(mBlacklist);
    }
}

std::vector<std::string> KRlvHandler::getBlacklist() const
{
    std::vector<std::string> result(mBlacklist.begin(), mBlacklist.end());
    std::sort(result.begin(), result.end());
    return result;
}

KRlv::KRlvObjectLists KRlvHandler::getObjectLists() const
{
    return mObjectLists;
}

void KRlvHandler::setObjectMode(KRlv::KRlvObjectMode mode)
{
    if (mObjectLists.mode != mode)
    {
        mObjectLists.mode = mode;
        KRlv::saveObjectLists(mObjectLists);
    }
}

void KRlvHandler::addObjectEntry(bool whitelist, const LLUUID& object, const LLUUID& owner)
{
    std::vector<KRlv::KRlvObjectEntry>& list = whitelist ? mObjectLists.whitelist : mObjectLists.blacklist;
    if (KRlv::objectListed(list, object, owner))
    {
        return;
    }
    KRlv::KRlvObjectEntry entry;
    entry.object = object.asString();
    entry.owner = owner.asString();
    list.push_back(entry);
    KRlv::saveObjectLists(mObjectLists);
}

void KRlvHandler::removeObjectEntry(bool whitelist, const LLUUID& object, const LLUUID& owner)
{
    std::vector<KRlv::KRlvObjectEntry>& list = whitelist ? mObjectLists.whitelist : mObjectLists.blacklist;
    const std::string object_text = object.asString();
    const std::string owner_text = owner.asString();
    const auto before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const KRlv::KRlvObjectEntry& entry)
    {
        return entry.object == object_text && entry.owner == owner_text;
    }), list.end());
    if (list.size() != before)
    {
        KRlv::saveObjectLists(mObjectLists);
    }
}

bool KRlvHandler::isDebugSettingProtected(const std::string& settingName) const
{
    // Same master-switch guard every other restriction-reading query
    // enforces at the read side - see feedback_krlv_master_switch_enforcement_point.
    if (!isEnabled())
    {
        return false;
    }
    return mProtectedDebugSettings.find(settingName) != mProtectedDebugSettings.end();
}

void KRlvHandler::addProtectedDebugSetting(const std::string& settingName)
{
    if (mProtectedDebugSettings.insert(settingName).second)
    {
        KRlv::saveProtectedDebugSettings(mProtectedDebugSettings);
    }
}

void KRlvHandler::removeProtectedDebugSetting(const std::string& settingName)
{
    if (mProtectedDebugSettings.erase(settingName) > 0)
    {
        KRlv::saveProtectedDebugSettings(mProtectedDebugSettings);
    }
}

std::vector<std::string> KRlvHandler::getProtectedDebugSettings() const
{
    std::vector<std::string> result(mProtectedDebugSettings.begin(), mProtectedDebugSettings.end());
    std::sort(result.begin(), result.end());
    return result;
}

void KRlvHandler::addRestriction(const std::string& behaviour, const LLUUID& sourceId, const std::string& option)
{
    auto& entries = mRestrictions[behaviour];
    for (const KRlvRestrictionEntry& entry : entries)
    {
        if (entry.sourceObjectId == sourceId)
        {
            // Same source re-asserting a restriction it already holds
            // (e.g. re-attached, or a script re-sending on region
            // change) - not a second, independent hold, so @notify
            // isn't fired again either.
            return;
        }
    }
    entries.push_back({ sourceId, option });
    KRlv::saveRestrictions(mRestrictions);
    notifyRestrictionChange(behaviour, option, NotifyKind::Added);
}

void KRlvHandler::removeRestriction(const std::string& behaviour, const LLUUID& sourceId)
{
    auto it = mRestrictions.find(behaviour);
    if (it == mRestrictions.end())
    {
        return;
    }
    auto& entries = it->second;
    std::string removedOption;
    bool removedAny = false;
    for (const KRlvRestrictionEntry& entry : entries)
    {
        if (entry.sourceObjectId == sourceId)
        {
            removedOption = entry.option;
            removedAny = true;
            break;
        }
    }
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&sourceId](const KRlvRestrictionEntry& entry) { return entry.sourceObjectId == sourceId; }),
        entries.end());
    if (entries.empty())
    {
        mRestrictions.erase(it);
    }
    KRlv::saveRestrictions(mRestrictions);
    if (removedAny)
    {
        notifyRestrictionChange(behaviour, removedOption, NotifyKind::Removed);
    }
}

std::vector<std::pair<std::string, std::string>> KRlvHandler::getActiveRules(const LLUUID& sourceId) const
{
    std::vector<std::pair<std::string, std::string>> result;
    if (!isEnabled())
    {
        return result;
    }
    for (const auto& [behaviour, entries] : mRestrictions)
    {
        for (const KRlvRestrictionEntry& entry : entries)
        {
            if (sourceId.isNull() || entry.sourceObjectId == sourceId)
            {
                result.emplace_back(behaviour, entry.option);
            }
        }
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> KRlvHandler::clearRestrictionsFrom(const std::string& filter, const LLUUID& sourceId)
{
    std::vector<std::pair<std::string, std::string>> cleared;
    for (auto it = mRestrictions.begin(); it != mRestrictions.end(); )
    {
        if (!filter.empty() && it->first.find(filter) == std::string::npos)
        {
            ++it;
            continue;
        }
        auto& entries = it->second;
        for (auto entryIt = entries.begin(); entryIt != entries.end(); )
        {
            if (entryIt->sourceObjectId == sourceId)
            {
                cleared.emplace_back(it->first, entryIt->option);
                entryIt = entries.erase(entryIt);
            }
            else
            {
                ++entryIt;
            }
        }
        if (entries.empty())
        {
            it = mRestrictions.erase(it);
        }
        else
        {
            ++it;
        }
    }
    if (!cleared.empty())
    {
        KRlv::saveRestrictions(mRestrictions);
        for (const auto& [behaviour, option] : cleared)
        {
            notifyRestrictionChange(behaviour, option, NotifyKind::Cleared);
        }
    }
    return cleared;
}

void KRlvHandler::notifyRestrictionChange(const std::string& behaviour, const std::string& option, NotifyKind kind)
{
    // Subscriptions are keys "notify:<channel;word>", one entry per subscribing object.
    static const std::string kNotifyPrefix = "notify:";
    for (const auto& [key, entries] : mRestrictions)
    {
        if (key.rfind(kNotifyPrefix, 0) != 0)
        {
            continue;
        }
        const std::string subscription = key.substr(kNotifyPrefix.size());
        const size_t semi = subscription.find(';');
        const std::string channelStr = (semi == std::string::npos) ? subscription : subscription.substr(0, semi);
        const std::string word = (semi == std::string::npos) ? std::string() : subscription.substr(semi + 1);
        // Word filter matches the command name, not a subscription key.
        const std::string commandName = behaviour.rfind(kNotifyPrefix, 0) == 0 ? std::string("notify") : behaviour;
        if (!word.empty() && commandName.find(word) == std::string::npos)
        {
            continue;
        }
        const S32 channel = std::atoi(channelStr.c_str());
        if (channel == 0)
        {
            continue;
        }

        std::string reply = "/" + behaviour;
        if (!option.empty())
        {
            reply += ":" + option;
        }
        switch (kind)
        {
            case NotifyKind::Added:   reply += "=n"; break;
            case NotifyKind::Removed: reply += "=y"; break;
            case NotifyKind::Cleared: break; // spec: "@clear will not add an equal sign"
        }
        for (size_t i = 0; i < entries.size(); ++i)
        {
            send_chat_from_viewer(reply, CHAT_TYPE_SHOUT, channel);
        }
    }
}

std::vector<std::string> KRlvHandler::getRestrictionOptions(const std::string& behaviour) const
{
    std::vector<std::string> result;
    if (!isEnabled())
    {
        return result;
    }
    auto it = mRestrictions.find(behaviour);
    if (it != mRestrictions.end())
    {
        result.reserve(it->second.size());
        for (const KRlvRestrictionEntry& entry : it->second)
        {
            result.push_back(entry.option);
        }
    }
    return result;
}

void KRlvHandler::setFloaterCloseHook(KRlvFloaterCloseRequest hook)
{
    mFloaterCloseHook = std::move(hook);
}

bool KRlvHandler::requestFloaterClose(const std::string& floaterName) const
{
    return mFloaterCloseHook && mFloaterCloseHook(floaterName);
}

void KRlvHandler::registerFloaterGate(const std::string& behaviour, const std::string& floaterName)
{
    mFloaterGates.emplace_back(behaviour, floaterName);
}

void KRlvHandler::setForceRotateHook(KRlvForceRotateRequest hook)
{
    mForceRotateHook = std::move(hook);
}

void KRlvHandler::requestForceRotate(const LLVector3& lookAtDirection) const
{
    if (mForceRotateHook)
    {
        mForceRotateHook(lookAtDirection);
    }
}

void KRlvHandler::setForceAdjustHeightHook(KRlvForceAdjustHeightRequest hook)
{
    mForceAdjustHeightHook = std::move(hook);
}

void KRlvHandler::requestForceAdjustHeight(F32 targetPelvisToFoot, F32 factor, F32 delta) const
{
    if (mForceAdjustHeightHook)
    {
        mForceAdjustHeightHook(targetPelvisToFoot, factor, delta);
    }
}

void KRlvHandler::setForceFovHook(KRlvForceFovRequest hook)
{
    mForceFovHook = std::move(hook);
}

void KRlvHandler::requestForceFov(F32 fovRadians) const
{
    if (mForceFovHook)
    {
        mForceFovHook(fovRadians);
    }
}

void KRlvHandler::setGetFovHook(KRlvGetFovRequest hook)
{
    mGetFovHook = std::move(hook);
}

F32 KRlvHandler::requestGetFov() const
{
    return mGetFovHook ? mGetFovHook() : 0.f;
}

void KRlvHandler::setForceTeleportHook(KRlvForceTeleportRequest hook)
{
    mForceTeleportHook = std::move(hook);
}

void KRlvHandler::requestForceTeleport(const LLVector3d& posGlobal) const
{
    if (mForceTeleportHook)
    {
        mForceTeleportHook(posGlobal);
    }
}

void KRlvHandler::setResolveRegionPositionHook(KRlvResolveRegionPositionRequest hook)
{
    mResolveRegionPositionHook = std::move(hook);
}

bool KRlvHandler::requestResolveRegionPosition(const std::string& regionName, const LLVector3& localPos, LLVector3d& outGlobalPos) const
{
    return mResolveRegionPositionHook ? mResolveRegionPositionHook(regionName, localPos, outGlobalPos) : false;
}

void KRlvHandler::setForceSitHook(KRlvForceSitRequest hook)
{
    mForceSitHook = std::move(hook);
}

void KRlvHandler::requestForceSit(const LLUUID& objectId) const
{
    if (mForceSitHook)
    {
        mForceSitHook(objectId);
    }
}

void KRlvHandler::setForceStandHook(KRlvForceStandRequest hook)
{
    mForceStandHook = std::move(hook);
}

void KRlvHandler::requestForceStand() const
{
    if (mForceStandHook)
    {
        mForceStandHook();
    }
}

void KRlvHandler::setForceSitGroundHook(KRlvForceSitGroundRequest hook)
{
    mForceSitGroundHook = std::move(hook);
}

void KRlvHandler::requestForceSitGround() const
{
    if (mForceSitGroundHook)
    {
        mForceSitGroundHook();
    }
}

void KRlvHandler::setGetSitIdHook(KRlvGetSitIdRequest hook)
{
    mGetSitIdHook = std::move(hook);
}

LLUUID KRlvHandler::requestGetSitId() const
{
    return mGetSitIdHook ? mGetSitIdHook() : LLUUID::null;
}

void KRlvHandler::setGetAgentStateHook(KRlvGetAgentStateRequest hook)
{
    mGetAgentStateHook = std::move(hook);
}

bool KRlvHandler::requestGetAgentState(LLVector3d& outPositionGlobal) const
{
    return mGetAgentStateHook ? mGetAgentStateHook(outPositionGlobal) : false;
}

void KRlvHandler::registerIdleTick(std::function<void()> callback)
{
    mIdleTickCallbacks.push_back(std::move(callback));
}

void KRlvHandler::setForceDetachHook(KRlvForceDetachRequest hook)
{
    mForceDetachHook = std::move(hook);
}

void KRlvHandler::requestForceDetach(const LLUUID& sourceId, const std::string& attachPointName) const
{
    if (mForceDetachHook)
    {
        mForceDetachHook(sourceId, attachPointName);
    }
}

void KRlvHandler::setGetOutfitLayersHook(KRlvGetOutfitLayersRequest hook)
{
    mGetOutfitLayersHook = std::move(hook);
}

std::string KRlvHandler::requestGetOutfitLayers(const std::string& partName) const
{
    return mGetOutfitLayersHook ? mGetOutfitLayersHook(partName) : std::string();
}

void KRlvHandler::setGetAttachPointsHook(KRlvGetAttachPointsRequest hook)
{
    mGetAttachPointsHook = std::move(hook);
}

std::string KRlvHandler::requestGetAttachPoints(const std::string& pointName) const
{
    return mGetAttachPointsHook ? mGetAttachPointsHook(pointName) : std::string();
}

void KRlvHandler::setForceSetGroupHook(KRlvForceSetGroupRequest hook)
{
    mForceSetGroupHook = std::move(hook);
}

void KRlvHandler::requestForceSetGroup(const std::string& groupName) const
{
    if (mForceSetGroupHook)
    {
        mForceSetGroupHook(groupName);
    }
}

void KRlvHandler::setGetGroupNameHook(KRlvGetGroupNameRequest hook)
{
    mGetGroupNameHook = std::move(hook);
}

std::string KRlvHandler::requestGetGroupName() const
{
    return mGetGroupNameHook ? mGetGroupNameHook() : std::string();
}

void KRlvHandler::setGetSharedRootHook(KRlvGetSharedRootRequest hook)
{
    mGetSharedRootHook = std::move(hook);
}

LLUUID KRlvHandler::requestGetSharedRoot() const
{
    return mGetSharedRootHook ? mGetSharedRootHook() : LLUUID::null;
}

void KRlvHandler::setListFolderHook(KRlvListFolderRequest hook)
{
    mListFolderHook = std::move(hook);
}

std::vector<KRlvInvEntry> KRlvHandler::requestListFolder(const LLUUID& folderId) const
{
    return mListFolderHook ? mListFolderHook(folderId) : std::vector<KRlvInvEntry>();
}

void KRlvHandler::setGetParentHook(KRlvGetParentRequest hook)
{
    mGetParentHook = std::move(hook);
}

bool KRlvHandler::requestGetParent(const LLUUID& id, std::string& outSelfName, LLUUID& outParentId) const
{
    return mGetParentHook && mGetParentHook(id, outSelfName, outParentId);
}

void KRlvHandler::setResolveWornHook(KRlvResolveWornRequest hook)
{
    mResolveWornHook = std::move(hook);
}

LLUUID KRlvHandler::requestResolveWorn(const std::string& pointOrLayerOrUuid) const
{
    return mResolveWornHook ? mResolveWornHook(pointOrLayerOrUuid) : LLUUID::null;
}

void KRlvHandler::setIsSharedItemHook(KRlvIsSharedItemRequest hook)
{
    mIsSharedItemHook = std::move(hook);
}

bool KRlvHandler::requestIsSharedItem(const LLUUID& itemId) const
{
    return mIsSharedItemHook && mIsSharedItemHook(itemId);
}

void KRlvHandler::setForceWearBatchHook(KRlvForceWearBatchRequest hook)
{
    mForceWearBatchHook = std::move(hook);
}

void KRlvHandler::requestForceWearBatch(const std::vector<LLUUID>& itemIds, bool replace) const
{
    if (mForceWearBatchHook)
    {
        mForceWearBatchHook(itemIds, replace);
    }
}

void KRlvHandler::setForceDetachBatchHook(KRlvForceDetachBatchRequest hook)
{
    mForceDetachBatchHook = std::move(hook);
}

void KRlvHandler::requestForceDetachBatch(const std::vector<LLUUID>& itemIds) const
{
    if (mForceDetachBatchHook)
    {
        mForceDetachBatchHook(itemIds);
    }
}

void KRlvHandler::setApplyForcedEnvironmentHook(KRlvApplyForcedEnvironmentRequest hook)
{
    mApplyForcedEnvironmentHook = std::move(hook);
}

void KRlvHandler::requestApplyForcedEnvironment(const std::map<std::string, F32>& forcedParams) const
{
    if (mApplyForcedEnvironmentHook)
    {
        mApplyForcedEnvironmentHook(forcedParams);
    }
}

void KRlvHandler::setGetLiveEnvironmentSettingHook(KRlvGetLiveEnvironmentSettingRequest hook)
{
    mGetLiveEnvironmentSettingHook = std::move(hook);
}

bool KRlvHandler::requestGetLiveEnvironmentSetting(const std::string& settingName, F32& outValue) const
{
    return mGetLiveEnvironmentSettingHook ? mGetLiveEnvironmentSettingHook(settingName, outValue) : false;
}

void KRlvHandler::setSendImHook(KRlvSendImRequest hook)
{
    mSendImHook = std::move(hook);
}

void KRlvHandler::requestSendIm(const LLUUID& toId, const std::string& message) const
{
    if (mSendImHook)
    {
        mSendImHook(toId, message);
    }
}

std::string KRlvHandler::handleConsoleCommand(const std::string& args)
{
    std::string trimmed = args;
    size_t start = trimmed.find_first_not_of(" \t");
    trimmed = (start == std::string::npos) ? std::string() : trimmed.substr(start);

    if (trimmed == "on")
    {
        setEnabled(true);
        return "KRLV enabled.";
    }
    if (trimmed == "off")
    {
        setEnabled(false);
        return "KRLV disabled.";
    }

    if (trimmed == "blacklist" || trimmed.rfind("blacklist ", 0) == 0)
    {
        return handleBlacklistConsoleCommand(trimmed.size() > 9 ? trimmed.substr(9) : std::string());
    }

    if (trimmed == "protectdebug" || trimmed.rfind("protectdebug ", 0) == 0)
    {
        return handleProtectedDebugConsoleCommand(trimmed.size() > 12 ? trimmed.substr(12) : std::string());
    }

    return "KRLV: " + std::string(isEnabled() ? "enabled" : "disabled")
        + ", " + std::to_string(registeredBehaviourCount()) + " known command(s)"
        + ", " + std::to_string(activeRestrictionCount()) + " active restriction(s)."
        + ", " + std::to_string(mBlacklist.size()) + " blacklisted command name(s)."
        + ", " + std::to_string(mProtectedDebugSettings.size()) + " protected debug setting(s)."
        + " (usage: krlv [on|off|blacklist [add|remove <name>]|protectdebug [add|remove <name>]])";
}

std::string KRlvHandler::handleBlacklistConsoleCommand(const std::string& rest)
{
    std::string trimmed = rest;
    size_t start = trimmed.find_first_not_of(" \t");
    trimmed = (start == std::string::npos) ? std::string() : trimmed.substr(start);

    if (trimmed.empty())
    {
        std::vector<std::string> names = getBlacklist();
        if (names.empty())
        {
            return "KRLV blacklist: empty.";
        }
        std::string joined;
        for (size_t i = 0; i < names.size(); ++i)
        {
            if (i > 0)
            {
                joined += ", ";
            }
            joined += names[i];
        }
        return "KRLV blacklist (" + std::to_string(names.size()) + "): " + joined;
    }

    const size_t sp = trimmed.find(' ');
    std::string verb = (sp == std::string::npos) ? trimmed : trimmed.substr(0, sp);
    std::string name = (sp == std::string::npos) ? std::string() : trimmed.substr(sp + 1);
    const size_t nameStart = name.find_first_not_of(" \t");
    name = (nameStart == std::string::npos) ? std::string() : name.substr(nameStart);
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    if (verb == "add" && !name.empty())
    {
        addToBlacklist(name);
        return "KRLV blacklist: added \"" + name + "\".";
    }
    if ((verb == "remove" || verb == "rem") && !name.empty())
    {
        removeFromBlacklist(name);
        return "KRLV blacklist: removed \"" + name + "\".";
    }
    return "KRLV blacklist: usage: krlv blacklist [add|remove <command_name>]";
}

std::string KRlvHandler::handleProtectedDebugConsoleCommand(const std::string& rest)
{
    std::string trimmed = rest;
    size_t start = trimmed.find_first_not_of(" \t");
    trimmed = (start == std::string::npos) ? std::string() : trimmed.substr(start);

    if (trimmed.empty())
    {
        std::vector<std::string> names = getProtectedDebugSettings();
        if (names.empty())
        {
            return "KRLV protected debug settings: empty.";
        }
        std::string joined;
        for (size_t i = 0; i < names.size(); ++i)
        {
            if (i > 0)
            {
                joined += ", ";
            }
            joined += names[i];
        }
        return "KRLV protected debug settings (" + std::to_string(names.size()) + "): " + joined;
    }

    const size_t sp = trimmed.find(' ');
    std::string verb = (sp == std::string::npos) ? trimmed : trimmed.substr(0, sp);
    std::string name = (sp == std::string::npos) ? std::string() : trimmed.substr(sp + 1);
    const size_t nameStart = name.find_first_not_of(" \t");
    name = (nameStart == std::string::npos) ? std::string() : name.substr(nameStart);
    // Deliberately NOT lowercased, unlike handleBlacklistConsoleCommand()
    // above - gSavedSettings control names are case-sensitive, and this
    // list is matched exactly against them (see isDebugSettingProtected()).

    if (verb == "add" && !name.empty())
    {
        addProtectedDebugSetting(name);
        return "KRLV protected debug settings: added \"" + name + "\".";
    }
    if ((verb == "remove" || verb == "rem") && !name.empty())
    {
        removeProtectedDebugSetting(name);
        return "KRLV protected debug settings: removed \"" + name + "\".";
    }
    return "KRLV protected debug settings: usage: krlv protectdebug [add|remove <setting_name>]";
}
