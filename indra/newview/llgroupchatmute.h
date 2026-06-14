/**
 * @file llgroupchatmute.h
 * @brief Manager for per-group chat muting
 *
 * Copyright (C) 2024
 * This manages which groups have chat messages disabled while still
 * allowing group notices and other group functionality.
 */

#ifndef LL_LLGROUPCHATMUTE_H
#define LL_LLGROUPCHATMUTE_H

#include "llsingleton.h"
#include "lluuid.h"
#include <set>

class LLGroupChatMute : public LLSingleton<LLGroupChatMute>
{
    LLSINGLETON(LLGroupChatMute);
    ~LLGroupChatMute();

public:
    // Check if group chat is muted for a specific group
    bool isGroupChatMuted(const LLUUID& group_id) const;
    
    // Set group chat mute status
    void setGroupChatMuted(const LLUUID& group_id, bool muted);
    
    // Load/save settings from disk
    void loadSettings();
    void saveSettings();

private:
    std::set<LLUUID> mMutedGroupChats;
    std::string getSettingsFilePath() const;
};

#endif // LL_LLGROUPCHATMUTE_H
