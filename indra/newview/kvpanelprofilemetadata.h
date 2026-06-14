/**
 * @file kvpanelprofilemetadata.h
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

#ifndef KV_KVPANELPROFILEMETADATA_H
#define KV_KVPANELPROFILEMETADATA_H

#include "llpanelavatar.h"
#include "llavatarpropertiesprocessor.h"

class LLScrollListCtrl;
class LLTextBox;

/**
 * KV Profile Metadata Panel
 * Displays ALL avatar profile data received from the server,
 * including hidden fields that LL doesn't show in standard UI.
 */
class KVPanelProfileMetadata
    : public LLPanelProfilePropertiesProcessorTab
{
public:
    KVPanelProfileMetadata();
    /*virtual*/ ~KVPanelProfileMetadata();

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void updateData() override;
    void processProperties(void* data, EAvatarProcessorType type) override;
    void resetData() override;

private:
    void addDataRow(const std::string& label, const std::string& value);
    void addSectionHeader(const std::string& header);
    std::string formatFlags(U32 flags);
    std::string formatDate(const LLDate& date);
    std::string getAccountAge(const LLDate& born_date);
    std::string formatBool(bool value);

    LLScrollListCtrl* mDataList;
};

#endif // KV_KVPANELPROFILEMETADATA_H
