/**
 * @file kvpanelprofilemetadata.cpp
 * @brief KV Profile Metadata Panel - Shows all available profile data
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
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

#include "llviewerprecompiledheaders.h"

#include "kvpanelprofilemetadata.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "llavatarpropertiesprocessor.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "lldateutil.h"

KVPanelProfileMetadata::KVPanelProfileMetadata()
:   LLPanelProfilePropertiesProcessorTab(),
    mDataList(NULL)
{
}

KVPanelProfileMetadata::~KVPanelProfileMetadata()
{
}

bool KVPanelProfileMetadata::postBuild()
{
    mDataList = getChild<LLScrollListCtrl>("metadata_list");
    if (mDataList)
    {
        mDataList->clearRows();
    }
    return true;
}

void KVPanelProfileMetadata::onOpen(const LLSD& key)
{
    LLPanelProfileTab::onOpen(key);
    resetData();
    updateData();
}

void KVPanelProfileMetadata::resetData()
{
    resetLoading();

    if (mDataList)
    {
        mDataList->clearRows();
        LLSD row;
        row["columns"][0]["column"] = "label";
        row["columns"][0]["value"] = "Loading profile data...";
        row["columns"][1]["column"] = "value";
        row["columns"][1]["value"] = "";
        mDataList->addElement(row);
    }
}

void KVPanelProfileMetadata::updateData()
{
    LLUUID avatar_id = getAvatarId();
    if (!getStarted() && avatar_id.notNull())
    {
        setIsLoading();
        LLAvatarPropertiesProcessor::getInstance()->sendAvatarPropertiesRequest(avatar_id);
    }
}

void KVPanelProfileMetadata::processProperties(void* data, EAvatarProcessorType type)
{
    if (APT_PROPERTIES == type || APT_PROPERTIES_LEGACY == type)
    {
        const LLAvatarData* avatar_data = static_cast<const LLAvatarData*>(data);
        if (!avatar_data || !mDataList)
        {
            return;
        }

        if (getAvatarId() != avatar_data->avatar_id)
        {
            return;
        }

        mDataList->clearRows();

        // === BASIC INFORMATION ===
        addSectionHeader("=== BASIC INFORMATION ===");
        addDataRow("Avatar ID", avatar_data->avatar_id.asString());
        addDataRow("Born On", formatDate(avatar_data->born_on));
        addDataRow("Account Age", getAccountAge(avatar_data->born_on));
        addDataRow("Age Hidden", formatBool(avatar_data->hide_age));
        addDataRow("Account Type", avatar_data->customer_type);
        if (!avatar_data->caption_text.empty())
        {
            addDataRow("Caption", avatar_data->caption_text);
        }
        else if (avatar_data->caption_index > 0)
        {
            addDataRow("Caption Index", llformat("%d", (S32)avatar_data->caption_index));
        }
        addDataRow("", ""); // Spacer

        // === FLAGS (All 6 bits decoded) ===
        addSectionHeader("=== FLAGS ===");
        addDataRow("Raw Flags", llformat("0x%08X", avatar_data->flags));
        addDataRow("  Allow Publish", formatBool(avatar_data->flags & AVATAR_ALLOW_PUBLISH));
        addDataRow("  Mature Publish", formatBool(avatar_data->flags & AVATAR_MATURE_PUBLISH));
        addDataRow("  Payment Identified", formatBool(avatar_data->flags & AVATAR_IDENTIFIED));
        addDataRow("  Payment Transacted", formatBool(avatar_data->flags & AVATAR_TRANSACTED));
        addDataRow("  Online Status", formatBool(avatar_data->flags & AVATAR_ONLINE));
        addDataRow("  Age Verified", formatBool(avatar_data->flags & AVATAR_AGEVERIFIED));
        addDataRow("", ""); // Spacer

        // === PROFILE CONTENT ===
        addSectionHeader("=== PROFILE CONTENT ===");
        addDataRow("2nd Life Image", avatar_data->image_id.asString());

        if (!avatar_data->about_text.empty())
        {
            addDataRow("About Text", llformat("%d characters", (S32)avatar_data->about_text.length()));
            // Show first 200 chars in multiple rows if needed
            std::string about = avatar_data->about_text;
            if (about.length() > 200)
            {
                addDataRow("  Preview", about.substr(0, 200) + "...");
                addDataRow("  ...", llformat("(%d more characters)", (S32)(about.length() - 200)));
            }
            else
            {
                addDataRow("  Full Text", about);
            }
        }
        else
        {
            addDataRow("About Text", "(empty)");
        }

        if (!avatar_data->fl_about_text.empty())
        {
            addDataRow("Real Life Text", llformat("%d characters", (S32)avatar_data->fl_about_text.length()));
            std::string fl_about = avatar_data->fl_about_text;
            if (fl_about.length() > 200)
            {
                addDataRow("  Preview", fl_about.substr(0, 200) + "...");
                addDataRow("  ...", llformat("(%d more characters)", (S32)(fl_about.length() - 200)));
            }
            else
            {
                addDataRow("  Full Text", fl_about);
            }
        }
        else
        {
            addDataRow("Real Life Text", "(empty)");
        }

        addDataRow("1st Life Image", avatar_data->fl_image_id.asString());
        addDataRow("", ""); // Spacer

        // === PARTNER ===
        if (avatar_data->partner_id.notNull())
        {
            addSectionHeader("=== PARTNER ===");
            addDataRow("Partner ID", avatar_data->partner_id.asString());
            addDataRow("", ""); // Spacer
        }

        // === WEB & NOTES ===
        addSectionHeader("=== WEB & NOTES ===");
        addDataRow("Profile URL", avatar_data->profile_url.empty() ? "(none)" : avatar_data->profile_url);

        if (!avatar_data->notes.empty())
        {
            addDataRow("Your Notes", llformat("%d characters", (S32)avatar_data->notes.length()));
            std::string notes = avatar_data->notes;
            if (notes.length() > 200)
            {
                addDataRow("  Preview", notes.substr(0, 200) + "...");
                addDataRow("  ...", llformat("(%d more characters)", (S32)(notes.length() - 200)));
            }
            else
            {
                addDataRow("  Full Text", notes);
            }
        }
        else
        {
            addDataRow("Your Notes", "(empty)");
        }
        addDataRow("", ""); // Spacer

        // === GROUPS ===
        if (!avatar_data->group_list.empty())
        {
            addSectionHeader(llformat("=== GROUPS (%d) ===", (S32)avatar_data->group_list.size()));

            for (const auto& group_data : avatar_data->group_list)
            {
                addDataRow("Group Name", group_data.group_name);
                addDataRow("  Group ID", group_data.group_id.asString());
                addDataRow("  Insignia", group_data.group_insignia_id.asString());
                addDataRow("  Title", group_data.group_title.empty() ? "(none)" : group_data.group_title);
                addDataRow("  Powers (hex)", llformat("0x%016llX", (unsigned long long)group_data.group_powers));

                // Decode some common powers
                std::string powers_decoded;
                if (group_data.group_powers & 0x1) powers_decoded += "Member ";
                if (group_data.group_powers & 0x2) powers_decoded += "Moderator ";
                if (group_data.group_powers & 0x8) powers_decoded += "Owner ";
                if (!powers_decoded.empty())
                {
                    addDataRow("  Powers", powers_decoded);
                }

                addDataRow("  Accept Notices", formatBool(group_data.accept_notices));
                addDataRow("", ""); // Spacer between groups
            }
        }

        // === PICKS ===
        if (!avatar_data->picks_list.empty())
        {
            addSectionHeader(llformat("=== PICKS (%d) ===", (S32)avatar_data->picks_list.size()));

            for (const auto& pick_data : avatar_data->picks_list)
            {
                addDataRow("Pick Name", pick_data.second);
                addDataRow("  Pick ID", pick_data.first.asString());
                addDataRow("", ""); // Spacer between picks
            }
        }

        // === SUMMARY ===
        addSectionHeader("=== SUMMARY ===");
        S32 total_fields = 0;
        S32 hidden_fields = 0;

        // Count fields
        total_fields += 14; // Basic + flags
        if (avatar_data->partner_id.notNull()) total_fields += 1;
        total_fields += avatar_data->group_list.size() * 6;
        total_fields += avatar_data->picks_list.size() * 2;

        // Hidden fields count (approximate)
        hidden_fields += 3; // 3 flags not shown in standard UI
        hidden_fields += 1; // profile_url
        if (!avatar_data->group_list.empty())
        {
            hidden_fields += avatar_data->group_list.size() * 2; // powers + notices per group
        }

        addDataRow("Total Data Fields", llformat("%d fields", total_fields));
        addDataRow("Hidden in Standard UI", llformat("%d fields (%d%%)", hidden_fields, (hidden_fields * 100) / total_fields));
        addDataRow("", "");
        addDataRow("Note", "Interests data completely ignored by client");
        addDataRow("", "(Server sends 5 fields, client discards all)");

    setLoaded();
    }
}

void KVPanelProfileMetadata::addSectionHeader(const std::string& header)
{
    if (!mDataList) return;

    LLSD row;
    row["columns"][0]["column"] = "label";
    row["columns"][0]["value"] = header;
    row["columns"][0]["font"] = "SansSerifBold";
    row["columns"][0]["color"] = LLColor4::yellow.getValue();

    row["columns"][1]["column"] = "value";
    row["columns"][1]["value"] = "";

    mDataList->addElement(row);
}

void KVPanelProfileMetadata::addDataRow(const std::string& label, const std::string& value)
{
    if (!mDataList) return;

    LLSD row;
    row["columns"][0]["column"] = "label";
    row["columns"][0]["value"] = label;
    row["columns"][0]["font"] = "SansSerifSmall";

    row["columns"][1]["column"] = "value";
    row["columns"][1]["value"] = value;
    row["columns"][1]["font"] = "SansSerifSmall";

    mDataList->addElement(row);
}

std::string KVPanelProfileMetadata::formatFlags(U32 flags)
{
    return llformat("0x%08X", flags);
}

std::string KVPanelProfileMetadata::formatDate(const LLDate& date)
{
    // Format: "YYYY-MM-DD HH:MM:SS UTC"
    std::string date_str = date.asString();

    // LL dates come as "YYYY-MM-DDTHH:MM:SSZ" - make it more readable
    // Replace T with space, remove Z
    size_t t_pos = date_str.find('T');
    if (t_pos != std::string::npos)
    {
        date_str[t_pos] = ' ';
    }
    size_t z_pos = date_str.find('Z');
    if (z_pos != std::string::npos)
    {
        date_str = date_str.substr(0, z_pos) + " UTC";
    }

    return date_str;
}

std::string KVPanelProfileMetadata::getAccountAge(const LLDate& born_date)
{
    // Calculate account age in years and days
    time_t now = time(NULL);
    time_t born = (time_t)born_date.secondsSinceEpoch();

    S32 total_days = (S32)((now - born) / 86400); // seconds to days
    S32 years = total_days / 365;
    S32 days = total_days % 365;

    if (years > 0)
    {
        return llformat("%d year%s, %d day%s", years, (years == 1 ? "" : "s"), days, (days == 1 ? "" : "s"));
    }
    else
    {
        return llformat("%d day%s", days, (days == 1 ? "" : "s"));
    }
}

std::string KVPanelProfileMetadata::formatBool(bool value)
{
    return value ? "Yes" : "No";
}
