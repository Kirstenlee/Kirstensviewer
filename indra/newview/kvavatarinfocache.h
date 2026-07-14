/**
 * @file kvavatarinfocache.h
 * @brief One-shot, session-lifetime cache of avatar account age for nearby-list display
 *
 * S24: sourced from the same AvatarPropertiesReply used by profile panels
 * (see KVPanelProfileMetadata). Each avatar_id is requested at most once per
 * session - never re-requested, never polled. Callers should call get() on
 * their own existing refresh cadence (e.g. the nearby list's 1s timer) and
 * simply display nothing/a placeholder until the entry becomes valid.
 *
 * Payment-info (identified/transacted flags) was deliberately left out: the
 * modern AgentProfile cap does not reliably surface it for third-party
 * avatars, so it never showed anything actionable in practice. That data is
 * still one profile-open away if genuinely needed.
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

#ifndef KV_KVAVATARINFOCACHE_H
#define KV_KVAVATARINFOCACHE_H

#include "llsingleton.h"
#include "llavatarpropertiesprocessor.h"
#include "lluuid.h"
#include "lldate.h"
#include <unordered_map>

class KVAvatarInfoCache : public LLSingleton<KVAvatarInfoCache>, public LLAvatarPropertiesObserver
{
    LLSINGLETON(KVAvatarInfoCache);
    virtual ~KVAvatarInfoCache();

public:
    struct Entry
    {
        LLDate born_on;
        bool valid = false;     // true once the reply has arrived
        bool requested = false; // true once a request has been sent - never repeated
    };

    // Returns the cached entry for avatar_id (valid=false if not fetched yet).
    // Fires a one-shot AvatarPropertiesRequest the first time a given id is
    // seen; never re-requests it afterwards, even if called every frame.
    const Entry& get(const LLUUID& avatar_id);

    void processProperties(void* data, EAvatarProcessorType type) override;

private:
    std::unordered_map<LLUUID, Entry> mCache;
};

#endif // KV_KVAVATARINFOCACHE_H
