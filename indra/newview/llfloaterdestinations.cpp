/**
 * @file llfloaterdestinations.h
 * @author Leyla Farazha
 * @brief floater for the destinations guide
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterdestinations.h"
#include "llbutton.h"
#include "llmediactrl.h"
#include "lluictrlfactory.h"
#include "llurl.h"
#include "llviewercontrol.h"
#include "llviewermedia.h"
#include "llweb.h"
#include "llpluginclassmedia.h"

namespace
{
    // The guide is a trusted page: it may only wander around secondlife.com and
    // lindenlab.com.  Anything else is handed to the external browser, so the
    // floater can never get stuck on a third-party page.
    bool isGuideHost(const std::string& url)
    {
        LLURI uri(url);
        std::string host = uri.hostName();
        LLStringUtil::toLower(host);
        auto ends_with = [&host](const std::string& tail)
        {
            return host == tail
                || (host.size() > tail.size() + 1
                    && host.compare(host.size() - tail.size() - 1, std::string::npos, "." + tail) == 0);
        };
        return ends_with("secondlife.com") || ends_with("lindenlab.com");
    }
}

LLFloaterDestinations::LLFloaterDestinations(const LLSD& key)
    :   LLFloater(key)
{
}

LLFloaterDestinations::~LLFloaterDestinations()
{
}

bool LLFloaterDestinations::postBuild()
{
    mBrowser = getChild<LLMediaCtrl>("destination_guide_contents");
    mBrowser->setErrorPageURL(gSavedSettings.getString("GenericErrorPageURL"));

    mBackBtn = getChild<LLButton>("back_btn");
    mBackBtn->setEnabled(false);
    mBackBtn->setCommitCallback([this](LLUICtrl*, const LLSD&) { mBrowser->navigateBack(); });
    getChild<LLButton>("home_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { goHome(); });
    getChild<LLButton>("browser_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        std::string url = mBrowser->getCurrentNavUrl();
        if (url.empty())
        {
            url = gSavedSettings.getString("DestinationGuideURL");
        }
        LLWeb::loadURLExternal(url);
    });

    goHome();
    mFreshlyBuilt = true;

    mBrowser->addObserver(this);
    return true;
}

void LLFloaterDestinations::goHome()
{
    std::string url = gSavedSettings.getString("DestinationGuideURL");
    mBrowser->navigateTo(url, std::string{HTTP_CONTENT_TEXT_HTML});

    // If cookie is there, will set it now. Otherwise will have to wait for login completion
    // which will also update destinations instance if it already exists.
    LLViewerMedia::getInstance()->getOpenIDCookie(mBrowser);
}

void LLFloaterDestinations::onOpen(const LLSD& key)
{
    // The first open already loaded the guide in postBuild(); every later open
    // starts from the guide again, so a page the user wandered off to is not
    // left behind.
    if (mFreshlyBuilt)
    {
        mFreshlyBuilt = false;
        return;
    }
    goHome();
}

void LLFloaterDestinations::handleMediaEvent(LLPluginClassMedia* self, EMediaEvent event)
{
    if (event == MEDIA_EVENT_NAVIGATE_BEGIN)
    {
        const std::string url = self->getNavigateURI();
        // The guide's own error page, its configured home URL and pages with no host
        // (about:blank, data:) are never bounced to the external browser
        const bool internal = url.empty() || url == mBrowser->getErrorPageURL()
            || url == gSavedSettings.getString("DestinationGuideURL")
            || LLURI(url).hostName().empty();
        if (!internal && !isGuideHost(url))
        {
            // not part of the guide: open it outside and stay on the guide
            mBrowser->navigateStop();
            LLWeb::loadURLExternal(url);
            if (mBrowser->canNavigateBack())
            {
                mBrowser->navigateBack();
            }
            else
            {
                goHome();
            }
            return;
        }
        mBackBtn->setEnabled(self->getHistoryBackAvailable());
    }
    else if (event == MEDIA_EVENT_NAVIGATE_COMPLETE)
    {
        mBackBtn->setEnabled(self->getHistoryBackAvailable());
    }
}
