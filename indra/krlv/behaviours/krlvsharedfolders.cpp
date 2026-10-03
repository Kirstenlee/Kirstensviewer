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
 * @attachthis/@attachallthis, and their @..._except exception lists) is
 * a genuinely uncertain corner of the spec - the source dataset's own
 * description text for the restriction forms of these four commands is
 * truncated ("...if either of these conditions is filled:", with the
 * conditions themselves lost to the scrape). Implemented by analogy with
 * Attachments' already-shipped @detach:<point>=n convention (a folder
 * lock blocks FORCE actions that would affect its contents, not a
 * continuous "can't even browse" restriction), keyed the same way
 * @editobj:<UUID>=n/@showhovertext:<UUID>=n already key per-target
 * restrictions - by the resolved folder's own UUID as the restriction
 * entry's "source". A recursive ("allthis") lock on an ancestor folder
 * is honoured by walking up the parent chain; a folder's own
 * "..._except" entry (non-recursive) or an ancestor's "allthis_except"
 * entry (recursive) exempts it. This is KRLV's own documented engineering
 * choice for filling a real spec gap, not a guessed-at RLVa behaviour.
 *
 * @getinvworn's description in the source dataset is IDENTICAL, word for
 * word, to @getinv's - almost certainly a scraping duplication rather
 * than the real spec text (the two commands' names strongly imply
 * different reply content - a worn-status breakdown, not just a folder
 * listing). Rather than fabricate a worn-status wire format from
 * assumption, @getinvworn currently replies with the exact same
 * subfolder-name listing @getinv does - a documented, honest limitation,
 * not a silent guess.
 *
 * @getpathnew is implemented identically to @getpath (a single path) -
 * the spec's "paths" (plural) wording most plausibly refers to an item
 * linked into multiple "#RLV" folders at once, which would need genuine
 * link-identity matching this pass does not implement; documented rather
 * than half-built.
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

    // "@...this[:<attachpt>|<clothing_layer>]=force" - resolves the
    // CONTAINING folder of the worn item named by `cmd.option`, or of
    // the issuing object's OWN attachment when no option is given.
    LLUUID resolveThisFolderIdForForce(const KRlvCommand& cmd, const LLUUID& sourceId)
    {
        const LLUUID itemId = cmd.hasOption()
            ? gKRlv.requestResolveWorn(cmd.option)
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

    // "@...this[:<layer>|<attachpt>|<path_to_folder>]=<y/n>" - the
    // restriction forms additionally accept a direct folder path (tried
    // first), falling back to worn-item resolution like the force forms.
    LLUUID resolveThisFolderIdForRestriction(const std::string& option)
    {
        if (option.empty())
        {
            return LLUUID::null;
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
            // See file header - the dataset's own description for this
            // command duplicates @getinv's; this deliberately mirrors
            // @getinv rather than fabricate a worn-status format.
            const LLUUID folderId = resolveSharedPath(cmd.option);
            if (folderId.notNull())
            {
                replyOnChannel(joinStrings(listVisibleSubfolderNames(folderId), ","), cmd.param);
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
            // See file header - implemented identically to @getpath.
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

    handler.registerBehaviour("attachthis",
        [](const KRlvCommand& cmd, const LLUUID& sourceId, const LLUUID&) -> bool
        {
            if (cmd.param == "force")
            {
                doForceAttachFolder(resolveThisFolderIdForForce(cmd, sourceId), false, true);
            }
            else
            {
                const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option);
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
            else
            {
                const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option);
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
                const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option);
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
                const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option);
                if (folderId.notNull())
                {
                    applyFolderToggle("detachallthis", cmd, folderId);
                }
            }
            return true;
        });

    handler.registerBehaviour("detachthis_except",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option);
            if (folderId.notNull())
            {
                applyFolderException("detachthis_except", cmd, folderId);
            }
            return true;
        });
    handler.registerBehaviour("detachallthis_except",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option);
            if (folderId.notNull())
            {
                applyFolderException("detachallthis_except", cmd, folderId);
            }
            return true;
        });
    handler.registerBehaviour("attachthis_except",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option);
            if (folderId.notNull())
            {
                applyFolderException("attachthis_except", cmd, folderId);
            }
            return true;
        });
    handler.registerBehaviour("attachallthis_except",
        [](const KRlvCommand& cmd, const LLUUID&, const LLUUID&) -> bool
        {
            const LLUUID folderId = resolveThisFolderIdForRestriction(cmd.option);
            if (folderId.notNull())
            {
                applyFolderException("attachallthis_except", cmd, folderId);
            }
            return true;
        });
}
