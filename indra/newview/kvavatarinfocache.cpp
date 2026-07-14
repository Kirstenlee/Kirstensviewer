/**
 * @file kvavatarinfocache.cpp
 * @brief One-shot, session-lifetime cache of avatar account age for nearby-list display
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
#include "kvavatarinfocache.h"

KVAvatarInfoCache::KVAvatarInfoCache()
{
}

KVAvatarInfoCache::~KVAvatarInfoCache()
{
}

const KVAvatarInfoCache::Entry& KVAvatarInfoCache::get(const LLUUID& avatar_id)
{
    Entry& entry = mCache[avatar_id]; // default-constructs (valid=false, requested=false) if new

    if (!entry.requested)
    {
        entry.requested = true;
        LLAvatarPropertiesProcessor::getInstance()->addObserver(avatar_id, this);
        LLAvatarPropertiesProcessor::getInstance()->sendAvatarPropertiesRequest(avatar_id);
    }

    return entry;
}

void KVAvatarInfoCache::processProperties(void* data, EAvatarProcessorType type)
{
    if (type != APT_PROPERTIES && type != APT_PROPERTIES_LEGACY)
    {
        return;
    }

    // S24: APT_PROPERTIES_LEGACY delivers LLAvatarLegacyData, not LLAvatarData - but the two
    // structs share identical layout up to and including "born_on" (see
    // llavatarpropertiesprocessor.h), which is all we read here. Same cast KVPanelProfileMetadata
    // already relies on for the same reason.
    const LLAvatarData* avatar_data = static_cast<const LLAvatarData*>(data);
    if (!avatar_data)
    {
        return;
    }

    Entry& entry = mCache[avatar_data->avatar_id];
    entry.born_on = avatar_data->born_on;
    entry.valid = true;

    // One-shot: we have what we need, stop listening for this avatar.
    LLAvatarPropertiesProcessor::getInstance()->removeObserver(avatar_data->avatar_id, this);
}
