/**
 * @file llgroupchatmute.cpp
 * @brief Manager for per-group chat muting
 *
 * Copyright (C) 2024
 */

#include "llviewerprecompiledheaders.h"

#include "llgroupchatmute.h"
#include "llsdserialize.h"
#include "llfile.h"
#include "lldir.h"

LLGroupChatMute::LLGroupChatMute()
{
    loadSettings();
}

LLGroupChatMute::~LLGroupChatMute()
{
    saveSettings();
}

bool LLGroupChatMute::isGroupChatMuted(const LLUUID& group_id) const
{
    return mMutedGroupChats.find(group_id) != mMutedGroupChats.end();
}

void LLGroupChatMute::setGroupChatMuted(const LLUUID& group_id, bool muted)
{
    if (muted)
    {
        mMutedGroupChats.insert(group_id);
    }
    else
    {
        mMutedGroupChats.erase(group_id);
    }
    saveSettings();
}

std::string LLGroupChatMute::getSettingsFilePath() const
{
    std::string filename = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "group_chat_mutes.xml");
    return filename;
}

void LLGroupChatMute::loadSettings()
{
    mMutedGroupChats.clear();
    
    std::string filename = getSettingsFilePath();
    if (!LLFile::isfile(filename))
    {
        return; // No settings file yet
    }
    
    llifstream file(filename.c_str());
    if (file.is_open())
    {
        LLSD data;
        if (LLSDSerialize::fromXMLDocument(data, file))
        {
            for (LLSD::array_const_iterator it = data.beginArray(); it != data.endArray(); ++it)
            {
                LLUUID group_id = it->asUUID();
                if (group_id.notNull())
                {
                    mMutedGroupChats.insert(group_id);
                }
            }
        }
        file.close();
    }
}

void LLGroupChatMute::saveSettings()
{
    std::string filename = getSettingsFilePath();
    
    LLSD data;
    for (std::set<LLUUID>::const_iterator it = mMutedGroupChats.begin(); it != mMutedGroupChats.end(); ++it)
    {
        data.append(*it);
    }
    
    llofstream file(filename.c_str());
    if (file.is_open())
    {
        LLSDSerialize::toPrettyXML(data, file);
        file.close();
    }
}
