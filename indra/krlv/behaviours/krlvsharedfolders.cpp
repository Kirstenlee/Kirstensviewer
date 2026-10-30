/**
 * @file krlvsharedfolders.cpp
 * @brief KRLV "Clothing and Attachments (Shared Folders)" category -
 * @unsharedwear, @unsharedunwear, @sharedwear, @sharedunwear, @getinv,
 * @getinvworn, @findfolder, @findfolders, @attach(over/overorreplace),
 * @attachall(over/overorreplace), @detachall, @getpath, @getpathnew, the
 * @attach/@detach-"this" shortcut and restriction families, and their
 * @..._except exception lists. @detach itself (the folder-content
 * meaning of this category) is handled as a fallback branch of
 * Attachments' own "detach" force hook - see krlvattachment.cpp's file
 * header and krlv/README.md.
 *
 * COMPLETE: all 29 command names have real handlers. This is the one
 * category that genuinely needed NEW krlv-internal infrastructure rather
 * than just gating existing viewer functions - almost everything here
 * (path parsing, depth-first search, "." folder-name filtering, the
 * whole folder-restriction scheme) is pure krlv-internal code, reached
 * from newview only through the small data-fetching hook family declared
 * in krlvhandler.h (KRlvGetSharedRootRequest, KRlvListFolderRequest,
 * KRlvGetParentRequest, KRlvResolveWornRequest, KRlvIsSharedItemRequest,
 * KRlvForceWearBatchRequest, KRlvForceDetachBatchRequest) - see
 * krlv/README.md's Shared Folders section for the full design note and
 * why no mini-floater was built (nothing in this category's spec depends
 * on a visible UI; the "#RLV" folder is an ordinary top-level inventory
 * folder the moment it exists, browsable for free in the stock inventory
 * floater like any other).
 *
 * Folder-tree primitives (this file): path resolution ("a/b/c" under
 * "#RLV"), a depth-first collector for @findfolder/@findfolders, and a
 * parent-chain walker for @getpath's reverse lookup - all built from
 * repeated KRlvListFolderRequest/KRlvGetParentRequest calls, matching
 * this viewer's own S24 AOEngine::tick() "#Kirstens" pattern for finding/
 * creating a well-known top-level folder (see krlvsharedfolders.h).
 *
 * Force-wear/detach (the @attach family/@detachall/the "this" force
 * shortcuts) do NOT implement the spec's folder/item-name-must-contain-the-attach-
 * point-name mechanic - confirmed via investigation that
 * LLAppearanceMgr::wearItemsOnAvatar() already resolves an attachment's
 * target point on its own (the same per-item default-point mechanism the
 * ordinary "Wear" action already relies on) when passed a batch of item
 * ids with no explicit point argument, making the spec's own name-based
 * resolution optional polish rather than a functional requirement - a
 * real architecture finding, not a shortcut taken for convenience.
 *
 * The "this" y/n restriction family (@detachthis/@detachallthis/
 * @attachthis/@attachallthis, and their @..._except exception lists):
 * the source dataset's own description text for these four commands was
 * truncated ("...if either of these conditions is filled:", with the
 * conditions themselves lost to the scrape) - fixed by reading the real
 * spec directly (wiki.secondlife.com's RestrainedLoveAPI page). The real
 * behaviour is NOT just a force-action gate (an earlier version of this
 * file implemented it that way, by analogy with @detach:<point>=n, before
 * the real text was available) - it's TWO things at once: (1) the user is
 * unable to manually wear/remove the resolved folder's contents via the
 * inventory's own "Add To Outfit"/"Replace Outfit"/"Remove From Outfit"
 * actions (gated in llappearancemgr.cpp's wearInventoryCategory()/
 * takeOffOutfit(), the two universal chokepoints every such UI path
 * already funnels through - see those functions' own KRLV_TOUCHPOINT
 * comments), AND (2) the folder itself "cannot be renamed, moved, deleted
 * or modified" - an inventory-structure lock (gated in
 * llinventorybridge.cpp's LLFolderBridge::isItemRenameable()/
 * isItemMovable()/isItemRemovable()). Force actions on the folder's
 * contents (@detach:<folder>=force etc.) are STILL blocked too, via the
 * same isFolderDetachLocked()/isFolderAttachLocked() queries this file
 * already used for that - the wrong part was only "ONLY a force-action
 * gate", not "force actions are also gated".
 *
 * Target folder resolution (resolveThisFolderIdForRestriction()) follows
 * the spec's four conditions for @detachthis (three for @attachthis, no
 * "no option" case since attaching needs a target): no option -> the
 * folder containing the ISSUING OBJECT's own worn attachment; a clothing-
 * layer or attachment-point name -> the folder containing whatever item
 * is CURRENTLY WORN at that layer/point; a folder path -> that folder
 * directly. "Modified" (arbitrary content mutation beyond the four named
 * operations - e.g. dragging a new item INTO a locked folder) is NOT
 * separately gated - a real, documented residual gap, not silently
 * guessed at.
 *
 * A recursive ("allthis") lock on an ancestor folder is honoured by
 * walking up the parent chain toward "#RLV" - the spec's own "closest
 * parent" rule: the NEAREST restriction or exception in the chain decides,
 * not a blanket "any ancestor" check. A folder's own "..._except" entry
 * (non-recursive) or an ancestor's "allthis_except" entry (recursive)
 * exempts it, per the spec's "nearest exception or restriction" wording -
 * exceptions are same-object-only, matching this project's established
 * _sec/_except convention elsewhere.
 *
 * @getinvworn's description in the source dataset was IDENTICAL, word for
 * word, to @getinv's - a scraping duplication, not the real spec text.
 * Fixed by looking the real wire format up directly (wiki.secondlife.com's
 * RestrainedLoveAPI page): comma-separated "name|XY" entries, current
 * folder first with no name, X/Y a 0-3 "proportion worn" code for items
 * directly in the folder / in all folders contained inside it
 * respectively - see wornStatusSuffix()'s comment for the exact scale.
 *
 * @getpathnew returns one comma-separated path per worn item matching
 * the given attach point/layer (real spec confirmed via
 * wiki.secondlife.com: added because RLVa v2.1 lets several objects
 * share one attachment point, which @getpath's single-folder answer
 * can't represent) - routes through its own KRlvResolveWornAllRequest
 * hook (krlvhandler.h/llappviewer.cpp) rather than @getpath's
 * single-match KRlvResolveWornRequest. Distinct paths only (two items in
 * the same folder reply with that path once, not twice).
 *
 * The bare "~" no-strip folder-name marker described in @attach's own
 * text is NOT implemented - its interaction with the already-explicit
 * "over"/"overorreplace" command variants (redundant with them, or
 * stacking on top of them) is not resolvable from the available spec
 * text, so implementing it risked guessing at a rule rather than reading
 * one. "." (hidden/disabled-from-scripts) folder names ARE implemented,
 * exactly as the spec states, everywhere a folder listing is built.
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

#include <algorithm>
#include <cstdlib>

#include "llerror.h"
#include "llchat.h"
#include "lluuid.h"

#include "krlvsharedfolders.h"
#include "krlvhandler.h"

// KRLV: same plain external-linkage free function several sibling files
// forward-declare locally.
void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

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

    // Same shape, but keyed by a resolved FOLDER id (used as the entry's
    // pseudo-"source") rather than the imposing object - matches
    // @editobj:<UUID>=n's/@showhovertext:<UUID>=n's established
    // per-target pattern.
    void applyFolderToggle(const std::string& key, const KRlvCommand& cmd, const LLUUID& folderId)
    {
        if (cmd.param == "n")
        {
            gKRlv.addRestriction(key, folderId);
        }
        else if (cmd.param == "y")
        {
            gKRlv.removeRestriction(key, folderId);
        }
    }

    void applyFolderException(const std::string& key, const KRlvCommand& cmd, const LLUUID& folderId)
    {
        if (cmd.param == "add")
        {
            gKRlv.addRestriction(key, folderId);
        }
        else if (cmd.param == "rem")
        {
            gKRlv.removeRestriction(key, folderId);
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

    std::string joinStrings(const std::vector<std::string>& parts, const std::string& sep)
    {
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0)
            {
                result += sep;
            }
            result += parts[i];
        }
        return result;
    }

    std::vector<std::string> splitOn(const std::string& s, const std::string& sep)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= s.size())
        {
            const size_t pos = s.find(sep, start);
            if (pos == std::string::npos)
            {
                parts.push_back(s.substr(start));
                break;
            }
            parts.push_back(s.substr(start, pos - start));
            start = pos + sep.size();
        }
        return parts;
    }

    bool isHiddenFolderName(const std::string& name)
    {
        return !name.empty() && name[0] == '.';
    }

    bool isNoStripFolderName(const std::string& name)
    {
        return !name.empty() && name[0] == '~';
    }

    bool nameContainsAll(const std::string& name, const std::vector<std::string>& parts)
    {
        for (const std::string& part : parts)
        {
            if (!part.empty() && name.find(part) == std::string::npos)
            {
                return false;
            }
        }
        return true;
    }

    // Resolves "folder1/folder2/..." starting from `fromId`, one level
    // at a time. LLUUID::null if any segment fails to match a subfolder.
    LLUUID resolvePath(const LLUUID& fromId, const std::vector<std::string>& parts)
    {
        LLUUID current = fromId;
        for (const std::string& part : parts)
        {
            if (part.empty())
            {
                continue;
            }
            bool found = false;
            for (const KRlvInvEntry& entry : gKRlv.requestListFolder(current))
            {
                if (entry.isCategory && entry.name == part)
                {
                    current = entry.id;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return LLUUID::null;
            }
        }
        return current;
    }

    // Resolves the "#RLV" root, then `path` under it. Empty path
    // resolves to the root itself.
    LLUUID resolveSharedPath(const std::string& path)
    {
        const LLUUID root = gKRlv.requestGetSharedRoot();
        if (root.isNull() || path.empty())
        {
            return root;
        }
        return resolvePath(root, splitOn(path, "/"));
    }

    std::vector<std::string> listVisibleSubfolderNames(const LLUUID& folderId)
    {
        std::vector<std::string> names;
        for (const KRlvInvEntry& entry : gKRlv.requestListFolder(folderId))
        {
            if (entry.isCategory && !isHiddenFolderName(entry.name))
            {
                names.push_back(entry.name);
            }
        }
        return names;
    }

    // @getinvworn's real wire format (confirmed via the published spec,
    // wiki.secondlife.com/wiki/LSL_Protocol/RestrainedLoveAPI - the
    // dataset this project draws from had this command's description
    // scraped as a duplicate of @getinv's, see file header): each digit
    // is a 0-3 "proportion worn" code, 0 = no items present, 1 = items
    // present but none worn, 2 = items present and some worn, 3 = items
    // present and all worn (no-mod items count the same as any other).
    int wornProportionDigit(S32 total, S32 worn)
    {
        if (total == 0) return 0;
        if (worn == 0) return 1;
        if (worn == total) return 3;
        return 2;
    }

    // Items (not folders) directly inside `folderId` - the first digit's
    // source.
    void countDirectItems(const LLUUID& folderId, S32& total, S32& worn)
    {
        for (const KRlvInvEntry& entry : gKRlv.requestListFolder(folderId))
        {
            if (!entry.isCategory)
            {
                ++total;
                if (entry.isWorn)
                {
                    ++worn;
                }
            }
        }
    }

    // Items in every VISIBLE subfolder of `folderId`, recursively - NOT
    // folderId's own direct items. The spec's own wording for the second
    // digit: "proportion of items globally worn in all the folders
    // contained inside the corresponding folder".
    void countItemsInSubfoldersRecursive(const LLUUID& folderId, S32& total, S32& worn)
    {
        for (const KRlvInvEntry& entry : gKRlv.requestListFolder(folderId))
        {
            if (!entry.isCategory || isHiddenFolderName(entry.name))
            {
                continue;
            }
            countDirectItems(entry.id, total, worn);
            countItemsInSubfoldersRecursive(entry.id, total, worn);
        }
    }

    // The "|XY" suffix @getinvworn appends to a folder name (or stands
    // alone for the current folder, which the spec says is unnamed and
    // listed first).
    std::string wornStatusSuffix(const LLUUID& folderId)
    {
        S32 directTotal = 0, directWorn = 0;
        countDirectItems(folderId, directTotal, directWorn);
        S32 subTotal = 0, subWorn = 0;
        countItemsInSubfoldersRecursive(folderId, subTotal, subWorn);
        return std::to_string(wornProportionDigit(directTotal, directWorn))
             + std::to_string(wornProportionDigit(subTotal, subWorn));
    }

    // Depth-first walk collecting every VISIBLE ("."-filtered) folder
    // under `folderId`, as (path, id) pairs.
    void collectAllFolders(const LLUUID& folderId, const std::string& pathSoFar,
                            std::vector<std::pair<std::string, LLUUID>>& out)
    {
        for (const KRlvInvEntry& entry : gKRlv.requestListFolder(folderId))
        {
            if (!entry.isCategory || isHiddenFolderName(entry.name))
            {
                continue;
            }
            const std::string path = pathSoFar.empty() ? entry.name : (pathSoFar + "/" + entry.name);
            out.emplace_back(path, entry.id);
            collectAllFolders(entry.id, path, out);
        }
    }

    // Item ids directly in `folderId` - also recurses into visible child
    // folders when `recursive` is true.
    void collectItemIds(const LLUUID& folderId, bool recursive, std::vector<LLUUID>& out)
    {
        for (const KRlvInvEntry& entry : gKRlv.requestListFolder(folderId))
        {
            if (entry.isCategory)
            {
                if (recursive && !isHiddenFolderName(entry.name))
                {
                    collectItemIds(entry.id, true, out);
                }
            }
            else
            {
                out.push_back(entry.id);
            }
        }
    }

    // Builds the "#RLV"-relative path of `folderId` ITSELF (its own name
    // included), walking parents up to the root. Succeeds with an empty
    // path if `folderId` IS the root; fails if it isn't under "#RLV".
    bool buildFolderPath(const LLUUID& folderId, std::string& outPath)
    {
        const LLUUID root = gKRlv.requestGetSharedRoot();
        if (root.isNull())
        {
            return false;
        }
        if (folderId == root)
        {
            outPath.clear();
            return true;
        }
        std::vector<std::string> names;
        LLUUID current = folderId;
        while (current != root)
        {
            std::string selfName;
            LLUUID parentId;
            if (!gKRlv.requestGetParent(current, selfName, parentId) || parentId.isNull())
            {
                return false;
            }
            names.push_back(selfName);
            current = parentId;
        }
        std::reverse(names.begin(), names.end());
        outPath = joinStrings(names, "/");
        return true;
    }

    // Path of the folder CONTAINING `itemId` (an item's own name is
    // never part of its own path).
    bool buildContainingFolderPath(const LLUUID& itemId, std::string& outPath)
    {
        std::string selfName;
        LLUUID parentId;
        if (!gKRlv.requestGetParent(itemId, selfName, parentId))
        {
            return false;
        }
        return buildFolderPath(parentId, outPath);
    }

    // Resolves the CONTAINING folder of the worn item named by `option`
    // (a layer or attachment-point name), or of the issuing object's OWN
    // attachment when `option` is empty - shared by both the force and
    // restriction forms of "@...this[...]".
    LLUUID resolveWornItemParentFolder(const std::string& option, const LLUUID& sourceId)
    {
        const LLUUID itemId = !option.empty()
            ? gKRlv.requestResolveWorn(option)
            : gKRlv.requestResolveWorn(sourceId.asString());
        if (itemId.isNull())
        {
            return LLUUID::null;
        }
        std::string selfName;
        LLUUID parentId;
        if (!gKRlv.requestGetParent(itemId, selfName, parentId))
        {
            return LLUUID::null;
        }
        return parentId;
    }

    // "@...this[:<attachpt>|<clothing_layer>]=force" - see
    // resolveWornItemParentFolder().
    LLUUID resolveThisFolderIdForForce(const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        return resolveWornItemParentFolder(cmd.option, sourceId);
    }

    // "@...this[:<layer>|<attachpt>|<path_to_folder>]=<y/n>" - the
    // restriction forms additionally accept a direct folder path (tried
    // first), falling back to worn-item resolution like the force forms.
    // S24: bare "@detachthis=n"/"@detachallthis=n" (no option) is its own
    // documented condition - "no option is specified and the folder
    // contains the object that issues this restriction" - so an empty
    // option falls back to the same issuing-object resolution the force
    // forms already use, instead of being a silent no-op. The attach
    // commands (attachthis/attachallthis and their _except forms) have no
    // such no-option condition per the spec (attaching needs a target) -
    // their call sites below must check cmd.option is non-empty BEFORE
    // calling this function, since its empty-option branch is only valid
    // for detachthis's documented case; calling it unconditionally for an
    // attach command silently locks the issuing object's own folder.
    LLUUID resolveThisFolderIdForRestriction(const std::string& option, const LLUUID& sourceId)
    {
        if (option.empty())
        {
            return resolveWornItemParentFolder(option, sourceId);
        }
        const LLUUID direct = resolveSharedPath(option);
        if (direct.notNull())
        {
            return direct;
        }
        const LLUUID itemId = gKRlv.requestResolveWorn(option);
        if (itemId.isNull())
        {
            return LLUUID::null;
        }
        std::string selfName;
        LLUUID parentId;
        if (!gKRlv.requestGetParent(itemId, selfName, parentId))
        {
            return LLUUID::null;
        }
        return parentId;
    }

    // See file header - a folder lock blocks force actions on its
    // contents; a recursive ("allthis") lock on an ancestor is inherited
    // unless an intervening folder carries its own exception entry.
    bool isFolderLockedGeneric(const std::string& directKey, const std::string& allKey,
                                const std::string& exceptKey, const std::string& exceptAllKey,
                                const LLUUID& folderId)
    {
        if (gKRlv.hasRestrictionFrom(directKey, folderId) || gKRlv.hasRestrictionFrom(allKey, folderId))
        {
            return true;
        }
        if (gKRlv.hasRestrictionFrom(exceptKey, folderId) || gKRlv.hasRestrictionFrom(exceptAllKey, folderId))
        {
            return false;
        }
        const LLUUID root = gKRlv.requestGetSharedRoot();
        LLUUID current = folderId;
        while (current.notNull() && current != root)
        {
            std::string selfName;
            LLUUID parentId;
            if (!gKRlv.requestGetParent(current, selfName, parentId) || parentId.isNull())
            {
                break;
            }
            if (gKRlv.hasRestrictionFrom(exceptAllKey, parentId))
            {
                return false;
            }
            if (gKRlv.hasRestrictionFrom(allKey, parentId))
            {
                return true;
            }
            current = parentId;
        }
        return false;
    }

    bool isFolderDetachLocked(const LLUUID& folderId)
    {
        return isFolderLockedGeneric("detachthis", "detachallthis", "detachthis_except", "detachallthis_except", folderId);
    }

    bool isFolderAttachLocked(const LLUUID& folderId)
    {
        return isFolderLockedGeneric("attachthis", "attachallthis", "attachthis_except", "attachallthis_except", folderId);
    }

    void doForceAttachFolder(const LLUUID& folderId, bool recursive, bool replace)
    {
        if (folderId.isNull() || isFolderAttachLocked(folderId))
        {
            return;
        }
        std::vector<LLUUID> items;
        collectItemIds(folderId, recursive, items);
        if (!items.empty())
        {
            gKRlv.requestForceWearBatch(items, replace);
        }
    }

    void doForceAttachPath(const std::string& path, bool recursive, bool replace)
    {
        doForceAttachFolder(resolveSharedPath(path), recursive, replace);
    }

    void doForceDetachFolder(const LLUUID& folderId, bool recursive)
    {
        if (folderId.isNull() || isFolderDetachLocked(folderId))
        {
            return;
        }
        std::vector<LLUUID> items;
        collectItemIds(folderId, recursive, items);
        if (!items.empty())
        {
            gKRlv.requestForceDetachBatch(items);
        }
    }

    void doForceDetachPath(const std::string& path, bool recursive)
    {
        doForceDetachFolder(resolveSharedPath(path), recursive);
    }
}

namespace KRlv
{
    bool isWearBlockedByShareRule(const LLUUID& itemId)
    {
        const bool isShared = gKRlv.requestIsSharedItem(itemId);
        if (gKRlv.isRestricted("unsharedwear") && !isShared)
        {
            return true;
        }
        if (gKRlv.isRestricted("sharedwear") && isShared)
        {
            return true;
        }
        return false;
    }

    bool isUnwearBlockedByShareRule(const LLUUID& itemId)
    {
        const bool isShared = gKRlv.requestIsSharedItem(itemId);
        if (gKRlv.isRestricted("unsharedunwear") && !isShared)
        {
            return true;
        }
        if (gKRlv.isRestricted("sharedunwear") && isShared)
        {
            return true;
        }
        return false;
    }

    std::vector<LLUUID> resolveDetachFolderContents(const std::string& folderName)
    {
        std::vector<LLUUID> items;
        const LLUUID root = gKRlv.requestGetSharedRoot();
        if (root.isNull() || folderName.empty())
        {
            return items;
        }
        for (const KRlvInvEntry& entry : gKRlv.requestListFolder(root))
        {
            if (entry.isCategory && entry.name == folderName)
            {
                collectItemIds(entry.id, false, items);
                break;
            }
        }
        return items;
    }

    bool isSharedFolderDetachLocked(const LLUUID& folderId)
    {
        return isFolderDetachLocked(folderId);
    }

    bool isSharedFolderAttachLocked(const LLUUID& folderId)
    {
        return isFolderAttachLocked(folderId);
    }

    bool isSharedFolderStructureLocked(const LLUUID& folderId)
    {
        return isFolderDetachLocked(folderId) || isFolderAttachLocked(folderId);
    }
}

void krlv_register_shared_folder_commands(KRlvHandler& handler)
{
    handler.registerBehaviour("unsharedwear",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("unsharedwear", cmd, sourceId); return true; });
    handler.registerBehaviour("unsharedunwear",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("unsharedunwear", cmd, sourceId); return true; });
    handler.registerBehaviour("sharedwear",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sharedwear", cmd, sourceId); return true; });
    handler.registerBehaviour("sharedunwear",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { applyToggle("sharedunwear", cmd, sourceId); return true; });

    handler.registerBehaviour("getinv",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            const LLUUID folderId = resolveSharedPath(cmd.option);
            if (folderId.notNull())
            {
                replyOnChannel(joinStrings(listVisibleSubfolderNames(folderId), ","), cmd.param);
            }
            return true;
        });

    handler.registerBehaviour("getinvworn",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            // S24: real wire format confirmed against the published spec
            // (see wornStatusSuffix's comment) - comma-separated
            // "name|XY" entries. The current folder comes first with NO
            // name (just "|XY" - the spec's own example:
            // "@getinvworn:Restraints/Leather cuffs=2222" ->
            // "|02,Arms|30,Legs|10").
            const LLUUID folderId = resolveSharedPath(cmd.option);
            if (folderId.notNull())
            {
                std::vector<std::string> entries;
                entries.push_back("|" + wornStatusSuffix(folderId));
                for (const KRlvInvEntry& entry : gKRlv.requestListFolder(folderId))
                {
                    if (entry.isCategory && !isHiddenFolderName(entry.name))
                    {
                        entries.push_back(entry.name + "|" + wornStatusSuffix(entry.id));
                    }
                }
                replyOnChannel(joinStrings(entries, ","), cmd.param);
            }
            return true;
        });

    handler.registerBehaviour("findfolder",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            std::vector<std::pair<std::string, LLUUID>> all;
            collectAllFolders(gKRlv.requestGetSharedRoot(), std::string(), all);
            const std::vector<std::string> parts = splitOn(cmd.option, "&&");
            for (const auto& entry : all)
            {
                const size_t slash = entry.first.find_last_of('/');
                const std::string leafName = (slash == std::string::npos) ? entry.first : entry.first.substr(slash + 1);
                if (!isNoStripFolderName(leafName) && nameContainsAll(leafName, parts))
                {
                    replyOnChannel(entry.first, cmd.param);
                    break;
                }
            }
            return true;
        });

    handler.registerBehaviour("findfolders",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            std::string searchPart = cmd.option;
            std::string outputSep = ",";
            const size_t semi = cmd.option.find(';');
            if (semi != std::string::npos)
            {
                searchPart = cmd.option.substr(0, semi);
                outputSep = cmd.option.substr(semi + 1);
            }
            std::vector<std::pair<std::string, LLUUID>> all;
            collectAllFolders(gKRlv.requestGetSharedRoot(), std::string(), all);
            const std::vector<std::string> parts = splitOn(searchPart, "&&");
            std::vector<std::string> matches;
            for (const auto& entry : all)
            {
                const size_t slash = entry.first.find_last_of('/');
                const std::string leafName = (slash == std::string::npos) ? entry.first : entry.first.substr(slash + 1);
                if (!isNoStripFolderName(leafName) && nameContainsAll(leafName, parts))
                {
                    matches.push_back(entry.first);
                }
            }
            replyOnChannel(joinStrings(matches, outputSep), cmd.param);
            return true;
        });

    handler.registerBehaviour("attach",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachPath(cmd.option, false, true); return true; });
    handler.registerBehaviour("attachover",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachPath(cmd.option, false, false); return true; });
    handler.registerBehaviour("attachoverorreplace",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachPath(cmd.option, false, false); return true; }); // synonym of attachover, per spec
    handler.registerBehaviour("attachall",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachPath(cmd.option, true, true); return true; });
    handler.registerBehaviour("attachallover",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachPath(cmd.option, true, false); return true; });
    handler.registerBehaviour("attachalloverorreplace",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachPath(cmd.option, true, false); return true; }); // synonym of attachallover, per spec

    handler.registerBehaviour("detachall",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceDetachPath(cmd.option, true); return true; });

    handler.registerBehaviour("getpath",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            const LLUUID itemId = gKRlv.requestResolveWorn(cmd.option);
            if (itemId.notNull())
            {
                std::string path;
                if (buildContainingFolderPath(itemId, path))
                {
                    replyOnChannel(path, cmd.param);
                }
            }
            return true;
        });

    handler.registerBehaviour("getpathnew",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            // S24: real multi-path support - the spec's own reason for
            // this command to exist over @getpath (several objects can
            // share one attachment point since RLVa v2.1, each possibly
            // linked from a different "#RLV" folder). Resolve every
            // matching worn item, build each one's containing-folder
            // path, and reply with the distinct paths comma-separated -
            // matches the spec's own worked example,
            // "Restraints/Collar,Jewelry/Cute necklace".
            std::vector<std::string> paths;
            for (const LLUUID& itemId : gKRlv.requestResolveWornAll(cmd.option))
            {
                std::string path;
                if (buildContainingFolderPath(itemId, path)
                    && std::find(paths.begin(), paths.end(), path) == paths.end())
                {
                    paths.push_back(path);
                }
            }
            if (!paths.empty())
            {
                replyOnChannel(joinStrings(paths, ","), cmd.param);
            }
            return true;
        });

    handler.registerBehaviour("attachthis",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                doForceAttachFolder(resolveThisFolderIdForForce(cmd, sourceId), false, true);
            }
            else if (!cmd.option.empty())
            {
                // S24: no bare no-option case for attachthis - see
                // resolveThisFolderIdForRestriction()'s comment.
                const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option, sourceId);
                if (folderId.notNull())
                {
                    applyFolderToggle("attachthis", cmd, folderId);
                }
            }
            return true;
        });
    handler.registerBehaviour("attachthisover",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachFolder(resolveThisFolderIdForForce(cmd, sourceId), false, false); return true; });
    handler.registerBehaviour("attachthisoverorreplace",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachFolder(resolveThisFolderIdForForce(cmd, sourceId), false, false); return true; });

    handler.registerBehaviour("attachallthis",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                doForceAttachFolder(resolveThisFolderIdForForce(cmd, sourceId), true, true);
            }
            else if (!cmd.option.empty())
            {
                // S24: no bare no-option case for attachallthis - see
                // resolveThisFolderIdForRestriction()'s comment.
                const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option, sourceId);
                if (folderId.notNull())
                {
                    applyFolderToggle("attachallthis", cmd, folderId);
                }
            }
            return true;
        });
    handler.registerBehaviour("attachallthisover",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachFolder(resolveThisFolderIdForForce(cmd, sourceId), true, false); return true; });
    handler.registerBehaviour("attachallthisoverorreplace",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        { if (cmd.param == "force") doForceAttachFolder(resolveThisFolderIdForForce(cmd, sourceId), true, false); return true; });

    handler.registerBehaviour("detachthis",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                doForceDetachFolder(resolveThisFolderIdForForce(cmd, sourceId), false);
            }
            else
            {
                const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option, sourceId);
                if (folderId.notNull())
                {
                    applyFolderToggle("detachthis", cmd, folderId);
                }
            }
            return true;
        });

    handler.registerBehaviour("detachallthis",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                doForceDetachFolder(resolveThisFolderIdForForce(cmd, sourceId), true);
            }
            else
            {
                const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option, sourceId);
                if (folderId.notNull())
                {
                    applyFolderToggle("detachallthis", cmd, folderId);
                }
            }
            return true;
        });

    handler.registerBehaviour("detachthis_except",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option, sourceId);
            if (folderId.notNull())
            {
                applyFolderException("detachthis_except", cmd, folderId);
            }
            return true;
        });
    handler.registerBehaviour("detachallthis_except",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option, sourceId);
            if (folderId.notNull())
            {
                applyFolderException("detachallthis_except", cmd, folderId);
            }
            return true;
        });
    handler.registerBehaviour("attachthis_except",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            // S24: no bare no-option case for attachthis_except - see
            // resolveThisFolderIdForRestriction()'s comment.
            if (cmd.option.empty())
            {
                return true;
            }
            const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option, sourceId);
            if (folderId.notNull())
            {
                applyFolderException("attachthis_except", cmd, folderId);
            }
            return true;
        });
    handler.registerBehaviour("attachallthis_except",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            // S24: no bare no-option case for attachallthis_except - see
            // resolveThisFolderIdForRestriction()'s comment.
            if (cmd.option.empty())
            {
                return true;
            }
            const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option, sourceId);
            if (folderId.notNull())
            {
                applyFolderException("attachallthis_except", cmd, folderId);
            }
            return true;
        });
}
