/**
 * @file llfloaterkrlvcatalog.h
 * @brief Generated from the KRLV tracker's command list (160 distinct RLVa command
 * names in 17 categories). Read-only data for the Restrictions tab. Regenerate from
 * the tracker if the list changes; do not edit by hand.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

#ifndef LL_LLFLOATERKRLVCATALOG_H
#define LL_LLFLOATERKRLVCATALOG_H

#include <cstddef>

struct KRLVCatalogEntry
{
    const char* behaviour;   // name as KRLV registers it, without the @
    const char* category;
    const char* title;
};

inline const KRLVCatalogEntry* krlvCatalog(size_t& count)
{
    static const KRLVCatalogEntry entries[] = {
        { "version", "Version Checking", "Automated version checking" },
        { "versionnew", "Version Checking", "Automated version checking" },
        { "versionnum", "Version Checking", "Automated version number checking" },
        { "versionnumbl", "Blacklist handling", "Automated version number checking, followed by the blacklist" },
        { "getblacklist", "Blacklist handling", "Get the contents of the blacklist, with a filter" },
        { "notify", "Miscellaneous", "Start/stop notifications on a private channel" },
        { "permissive", "Miscellaneous", "Allow/deny permissive exceptions" },
        { "clear", "Miscellaneous", "Clear all the rules tied to an object" },
        { "getstatus", "Miscellaneous", "Get the list of restrictions the avatar is currently submitted to" },
        { "getstatusall", "Miscellaneous", "Get the list of all the restrictions the avatar is currently submitted to" },
        { "fly", "Movement", "Allow/prevent flying" },
        { "temprun", "Movement", "Allow/prevent running by double-tapping an arrow key" },
        { "alwaysrun", "Movement", "Allow/prevent always running" },
        { "setrot", "Movement", "Force rotate the avatar to a set direction" },
        { "adjustheight", "Movement", "Change the height of the avatar" },
        { "camzoommax", "Camera and view", "Allow/prevent zooming too far forward with Ctrl-0" },
        { "camzoommin", "Camera and view", "Allow/prevent zooming too far back with Ctrl-8" },
        { "setcam_fovmin", "Camera and view", "Allow/prevent zooming too far forward with Ctrl-0" },
        { "setcam_fovmax", "Camera and view", "Allow/prevent zooming too far back with Ctrl-8" },
        { "setcam_fov", "Camera and view", "Change the field of view" },
        { "camdistmax", "Camera and view", "Allow/prevent moving the camera too far from the avatar" },
        { "setcam_avdistmax", "Camera and view", "Allow/prevent moving the camera too far from the avatar" },
        { "camdistmin", "Camera and view", "Allow/prevent moving the camera too close to the avatar" },
        { "setcam_avdistmin", "Camera and view", "Allow/prevent moving the camera too close to the avatar" },
        { "camdrawmin", "Camera and view", "Partially or completely blind the avatar" },
        { "camdrawcolor", "Camera and view", "Specify the color of the fog" },
        { "camunlock", "Camera and view", "Allow/prevent unlocking the camera from the avatar" },
        { "setcam_unlock", "Camera and view", "Allow/prevent unlocking the camera from the avatar" },
        { "camavdist", "Camera and view", "Turn all the avatars to silhouettes beyond a certain distance" },
        { "camtextures", "Camera and view", "Turn all the textures blank, except for the avatars" },
        { "setcam_textures", "Camera and view", "Turn all the textures blank, except for the avatars" },
        { "getcam_avdistmin", "Camera and view", "Get the current minimum camera distance the user is restricted to" },
        { "getcam_avdistmax", "Camera and view", "Get the current maximum camera distance the user is restricted to" },
        { "getcam_fovmin", "Camera and view", "Get the current minimum field of view angle the user is restricted to" },
        { "getcam_fovmax", "Camera and view", "Get the current maximum field of view angle the user is restricted to" },
        { "getcam_zoommin", "Camera and view", "Get the current minimum zoom multiplier the user is restricted to" },
        { "getcam_fov", "Camera and view", "Get the current field of view the user has zoomed to" },
        { "sendchat", "Chat, Emotes and Instant Messages", "Allow/prevent sending chat messages" },
        { "chatshout", "Chat, Emotes and Instant Messages", "Allow/prevent shouting" },
        { "chatnormal", "Chat, Emotes and Instant Messages", "Allow/prevent chatting at normal volume" },
        { "chatwhisper", "Chat, Emotes and Instant Messages", "Allow/prevent whispering" },
        { "redirchat", "Chat, Emotes and Instant Messages", "Redirect public chat to private channels" },
        { "recvchat", "Chat, Emotes and Instant Messages", "Allow/prevent receiving chat messages" },
        { "recvchat_sec", "Chat, Emotes and Instant Messages", "Allow/prevent receiving chat messages, secure way" },
        { "recvchatfrom", "Chat, Emotes and Instant Messages", "Allow/prevent receiving chat messages from someone in particular" },
        { "sendgesture", "Chat, Emotes and Instant Messages", "Allow/prevent triggering a gesture" },
        { "emote", "Chat, Emotes and Instant Messages", "Remove/add an exception to the emote truncation above" },
        { "rediremote", "Chat, Emotes and Instant Messages", "Redirect public emotes to private channels" },
        { "recvemote", "Chat, Emotes and Instant Messages", "Allow/prevent seeing emotes" },
        { "recvemotefrom", "Chat, Emotes and Instant Messages", "Allow/prevent receiving emotes seen in public chat from someone in particular" },
        { "recvemote_sec", "Chat, Emotes and Instant Messages", "Allow/prevent seeing emotes, secure way" },
        { "sendchannel", "Chat, Emotes and Instant Messages", "Allow/prevent using any chat channel but certain channels" },
        { "sendchannel_sec", "Chat, Emotes and Instant Messages", "Allow/prevent using any chat channel but certain channels, secure way" },
        { "sendchannel_except", "Chat, Emotes and Instant Messages", "Allow/prevent using a particular chat channel" },
        { "sendim", "Chat, Emotes and Instant Messages", "Allow/prevent sending instant messages" },
        { "sendim_sec", "Chat, Emotes and Instant Messages", "Allow/prevent sending instant messages, secure way" },
        { "sendimto", "Chat, Emotes and Instant Messages", "Allow/prevent sending instant messages to someone in particular" },
        { "startim", "Chat, Emotes and Instant Messages", "Allow/prevent starting an IM session with anyone" },
        { "startimto", "Chat, Emotes and Instant Messages", "Allow/prevent starting an IM session with someone in particular" },
        { "recvim", "Chat, Emotes and Instant Messages", "Allow/prevent receiving instant messages" },
        { "recvim_sec", "Chat, Emotes and Instant Messages", "Allow/prevent receiving instant messages, secure way" },
        { "recvimfrom", "Chat, Emotes and Instant Messages", "Allow/prevent receiving instant messages from someone in particular" },
        { "tplocal", "Teleportation", "Allow/prevent teleporting locally" },
        { "tplm", "Teleportation", "Allow/prevent teleporting to a landmark" },
        { "tploc", "Teleportation", "Allow/prevent teleporting to a location" },
        { "tplure", "Teleportation", "Allow/prevent teleporting by a friend" },
        { "tplure_sec", "Teleportation", "Allow/prevent teleporting by a friend, secure way" },
        { "sittp", "Teleportation", "Unlimit/limit sit-tp" },
        { "standtp", "Teleportation", "Allow/prevent standing up at a different location than where we sat down" },
        { "tpto", "Teleportation", "Force-Teleport the user" },
        { "accepttp", "Teleportation", "Remove/add auto-accept teleport offers from a particular avatar" },
        { "accepttprequest", "Teleportation", "Remove/add auto-accept teleport requests from a particular avatar" },
        { "tprequest", "Teleportation", "Allow/prevent receiving teleport offers from people" },
        { "tprequest_sec", "Teleportation", "Allow/prevent receiving teleport offers from people, secure way" },
        { "showinv", "Inventory, Editing and Rezzing", "Allow/prevent using inventory" },
        { "viewnote", "Inventory, Editing and Rezzing", "Allow/prevent reading notecards" },
        { "viewscript", "Inventory, Editing and Rezzing", "Allow/prevent opening scripts" },
        { "viewtexture", "Inventory, Editing and Rezzing", "Allow/prevent opening textures" },
        { "edit", "Inventory, Editing and Rezzing", "Allow/prevent editing objects" },
        { "rez", "Inventory, Editing and Rezzing", "Allow/prevent rezzing inventory" },
        { "editobj", "Inventory, Editing and Rezzing", "Allow/prevent editing particular objects" },
        { "editworld", "Inventory, Editing and Rezzing", "Allow/prevent editing in-world objects" },
        { "editattach", "Inventory, Editing and Rezzing", "Allow/prevent editing attachments" },
        { "share", "Inventory, Editing and Rezzing", "Allow/prevent giving inventory to people" },
        { "share_sec", "Inventory, Editing and Rezzing", "Allow/prevent giving inventory to people, secure way" },
        { "unsit", "Sitting", "Allow/prevent standing up" },
        { "sit", "Sitting", "Force sit on an object" },
        { "getsitid", "Sitting", "Get the UUID of the object the avatar is sitting on" },
        { "sitground", "Sitting", "Force sit on the ground" },
        { "detach", "Clothing and Attachments", "Render an object detachable/nondetachable" },
        { "addattach", "Clothing and Attachments", "Unlock/Lock an attachment point empty" },
        { "remattach", "Clothing and Attachments", "Unlock/Lock an attachment point full" },
        { "defaultwear", "Clothing and Attachments", "Allow/deny the \"Wear\" contextual menu" },
        { "addoutfit", "Clothing and Attachments", "Allow/prevent wearing clothes" },
        { "remoutfit", "Clothing and Attachments", "Allow/prevent removing clothes" },
        { "getoutfit", "Clothing and Attachments", "Get the list of worn clothes" },
        { "getattach", "Clothing and Attachments", "Get the list of worn attachments" },
        { "acceptpermission", "Clothing and Attachments", "Force the viewer to automatically accept attach and take control permission requests" },
        { "denypermission", "Clothing and Attachments", "Allow/prevent accepting attach and take control permissions" },
        { "detachme", "Clothing and Attachments", "Force detach an item" },
        { "unsharedwear", "Clothing and Attachments (Shared Folders)", "Allow/prevent wearing clothes and attachments that are not part of the #RLV folder" },
        { "unsharedunwear", "Clothing and Attachments (Shared Folders)", "Allow/prevent removing clothes and attachments that are not part of the #RLV folder" },
        { "sharedwear", "Clothing and Attachments (Shared Folders)", "Allow/prevent wearing clothes and attachments that are part of the #RLV folder" },
        { "sharedunwear", "Clothing and Attachments (Shared Folders)", "Allow/prevent removing clothes and attachments that are part of the #RLV folder" },
        { "getinv", "Clothing and Attachments (Shared Folders)", "Get the list of shared folders in the avatar's inventory" },
        { "getinvworn", "Clothing and Attachments (Shared Folders)", "Get the list of shared folders in the avatar's inventory, with information about worn items" },
        { "findfolder", "Clothing and Attachments (Shared Folders)", "Get the path to a shared folder by giving a search criterion" },
        { "findfolders", "Clothing and Attachments (Shared Folders)", "Get the path to several shared folders by giving a search criterion" },
        { "attach", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder" },
        { "attachover", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder, without replacing what is already being worn" },
        { "attachoverorreplace", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder" },
        { "attachall", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder, and its children recursively" },
        { "attachallover", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder, and its children recursively, without replacing what is already being worn" },
        { "attachalloverorreplace", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder, and its children recursively" },
        { "detachall", "Clothing and Attachments (Shared Folders)", "Force detach items contained inside a shared folder, and its children recursively" },
        { "getpath", "Clothing and Attachments (Shared Folders)", "Get the path to the shared folder containing a particular object/clothing worn on a point" },
        { "getpathnew", "Clothing and Attachments (Shared Folders)", "Get the all paths to the shared folders containing the objects/clothing worn on a point" },
        { "attachthis", "Clothing and Attachments (Shared Folders)", "Force attach items contained into a shared folder that contains a particular object/clothing" },
        { "attachthisover", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder, without replacing what is already being worn" },
        { "attachthisoverorreplace", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder" },
        { "attachallthis", "Clothing and Attachments (Shared Folders)", "Force attach items contained into a shared folder that contains a particular object/clothing, and its children folders" },
        { "attachallthisover", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder, without replacing what is already being worn" },
        { "attachallthisoverorreplace", "Clothing and Attachments (Shared Folders)", "Force attach items contained inside a shared folder" },
        { "detachthis", "Clothing and Attachments (Shared Folders)", "Force detach items contained into a shared folder that contains a particular object/clothing" },
        { "detachallthis", "Clothing and Attachments (Shared Folders)", "Force detach items contained into a shared folder that contains a particular object/clothing, and its children folders" },
        { "detachthis_except", "Clothing and Attachments (Shared Folders)", "Remove/add exceptions to the detachallthis restriction, for one folder only" },
        { "detachallthis_except", "Clothing and Attachments (Shared Folders)", "Remove/add exceptions to the detachallthis restriction, for one folder and its children" },
        { "attachthis_except", "Clothing and Attachments (Shared Folders)", "Remove/add exceptions to the attachallthis restriction, for one folder only" },
        { "attachallthis_except", "Clothing and Attachments (Shared Folders)", "Remove/add exceptions to the attachallthis restriction, for one folder and its children" },
        { "fartouch", "Touch", "Allow/prevent touching objects located further than 1.5 meters away from the avatar" },
        { "touchfar", "Touch", "Allow/prevent touching objects located further than 1.5 meters away from the avatar" },
        { "touchall", "Touch", "Allow/prevent touching any objects" },
        { "touchworld", "Touch", "Allow/prevent touching objects in-world" },
        { "touchthis", "Touch", "Allow/prevent touching one object in particular" },
        { "touchme", "Touch", "Remove/add an exception to the touch* preventions, for one object only" },
        { "touchattach", "Touch", "Allow/prevent touching attachments" },
        { "touchattachself", "Touch", "Allow/prevent touching one's attachments" },
        { "touchattachother", "Touch", "Allow/prevent touching other people's attachments" },
        { "touchhud", "Touch", "Allow/prevent touching HUDs" },
        { "interact", "Touch", "Allow/prevent touching objects or attachments, editing, or rezzing" },
        { "showworldmap", "Location", "Allow/prevent viewing the world map" },
        { "showminimap", "Location", "Allow/prevent viewing the mini map" },
        { "showloc", "Location", "Allow/prevent knowing the current location" },
        { "shownames", "Name Tags and Hovertext", "Allow/prevent seeing the names of the people around" },
        { "shownames_sec", "Name Tags and Hovertext", "Allow/prevent seeing the names of the people around, secure way" },
        { "shownametags", "Name Tags and Hovertext", "Allow/prevent seeing the names of the people around, without censorship" },
        { "shownearby", "Name Tags and Hovertext", "Allow/prevent seeing people in the Nearby window" },
        { "showhovertextall", "Name Tags and Hovertext", "Allow/prevent seeing all the hovertexts" },
        { "showhovertext", "Name Tags and Hovertext", "Allow/prevent seeing one hovertext in particular" },
        { "showhovertexthud", "Name Tags and Hovertext", "Allow/prevent seeing the hovertexts on the HUD of the user" },
        { "showhovertextworld", "Name Tags and Hovertext", "Allow/prevent seeing the hovertexts in-world" },
        { "setgroup", "Group", "Force the agent to change the active group" },
        { "getgroup", "Group", "Get the name of the active group" },
        { "setdebug", "Viewer Control", "Allow/prevent changing some debug settings" },
        { "setdebug_", "Viewer Control", "Force change a debug setting" },
        { "getdebug_", "Viewer Control", "Get the value of a debug setting" },
        { "setenv", "Viewer Control", "Allow/prevent changing the environment settings" },
        { "setenv_", "Viewer Control", "Force change an environment setting" },
        { "getenv_", "Viewer Control", "Get the value of an environment setting" },
        { "allowidle", "Unofficial Commands", "Allow/prevent the disabling of the automatic Away/AFK indicator" },
    };
    count = sizeof(entries) / sizeof(entries[0]);
    return entries;
}

#endif // LL_LLFLOATERKRLVCATALOG_H
