/**
 * @file llfloaterbeacons.cpp
 * @brief Front-end to LLPipeline controls for highlighting various kinds of objects.
 * @author Coco
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
#include "llfloaterbeacons.h"
#include "llviewercontrol.h"
#include "lluictrlfactory.h"
#include "llcheckboxctrl.h"
#include "pipeline.h"
#include "dxpipeline.h"


LLFloaterBeacons::LLFloaterBeacons(const LLSD& seed)
:   LLFloater(seed)
{
    // Initialize pipeline states from saved settings.
    // OK to do at floater constructor time because beacons do not display unless the floater is open
    // therefore it is OK to not initialize the pipeline state before needed.
    // Note also that we should replace those pipeline statics with direct lookup of the saved settings
    // eliminating the need to keep these states in sync.
    DXPipeline::setRenderScriptedTouchBeacons(gSavedSettings.getBOOL("scripttouchbeacon"));
    DXPipeline::setRenderScriptedBeacons(     gSavedSettings.getBOOL("scriptsbeacon"));
    DXPipeline::setRenderPhysicalBeacons(     gSavedSettings.getBOOL("physicalbeacon"));
    DXPipeline::setRenderSoundBeacons(        gSavedSettings.getBOOL("soundsbeacon"));
    DXPipeline::setRenderParticleBeacons(     gSavedSettings.getBOOL("particlesbeacon"));
    DXPipeline::setRenderHighlights(          gSavedSettings.getBOOL("renderhighlights"));
    DXPipeline::setRenderBeacons(             gSavedSettings.getBOOL("renderbeacons"));
    DXPipeline::setRenderMOAPBeacons(         gSavedSettings.getBOOL("moapbeacon"));
    mCommitCallbackRegistrar.add("Beacons.UICheck", boost::bind(&LLFloaterBeacons::onClickUICheck, this,_1));
}

bool LLFloaterBeacons::postBuild()
{
    return true;
}

// Callback attached to each check box control to both affect their main purpose
// and to implement the couple screwy interdependency rules that some have.

void LLFloaterBeacons::onClickUICheck(LLUICtrl *ctrl)
{
    LLCheckBoxCtrl *check = (LLCheckBoxCtrl *)ctrl;
    std::string name = check->getName();
    if(name == "touch_only")
    {
        DXPipeline::toggleRenderScriptedTouchBeacons();
        // Don't allow both to be ON at the same time. Toggle the other one off if both now on.
        if (
            DXPipeline::getRenderScriptedTouchBeacons() &&
            DXPipeline::getRenderScriptedBeacons() )
        {
            DXPipeline::setRenderScriptedBeacons(false);
            getChild<LLCheckBoxCtrl>("scripted")->setControlValue(LLSD(false));
            getChild<LLCheckBoxCtrl>("scripted")->setValue(false);
            getChild<LLCheckBoxCtrl>("touch_only")->setControlValue(LLSD(true)); // just to be sure it's in sync with llpipeline
            getChild<LLCheckBoxCtrl>("touch_only")->setValue(true);
        }
    }
    else if(name == "scripted")
    {
        DXPipeline::toggleRenderScriptedBeacons();
        // Don't allow both to be ON at the same time. Toggle the other one off if both now on.
        if (
            DXPipeline::getRenderScriptedTouchBeacons() &&
            DXPipeline::getRenderScriptedBeacons() )
        {
            DXPipeline::setRenderScriptedTouchBeacons(false);
            getChild<LLCheckBoxCtrl>("touch_only")->setControlValue(LLSD(false));
            getChild<LLCheckBoxCtrl>("touch_only")->setValue(false);
            getChild<LLCheckBoxCtrl>("scripted")->setControlValue(LLSD(true)); // just to be sure it's in sync with llpipeline
            getChild<LLCheckBoxCtrl>("scripted")->setValue(true);
        }
    }
    else if(name == "physical")       DXPipeline::setRenderPhysicalBeacons(check->get());
    else if(name == "sounds")         DXPipeline::setRenderSoundBeacons(check->get());
    else if(name == "particles")      DXPipeline::setRenderParticleBeacons(check->get());
    else if(name == "moapbeacon")     DXPipeline::setRenderMOAPBeacons(check->get());
    else if(name == "highlights")
    {
        DXPipeline::toggleRenderHighlights();
        // Don't allow both to be OFF at the same time. Toggle the other one on if both now off.
        if (
            !DXPipeline::getRenderBeacons() &&
            !DXPipeline::getRenderHighlights() )
        {
            DXPipeline::setRenderBeacons(true);
            getChild<LLCheckBoxCtrl>("beacons")->setControlValue(LLSD(true));
            getChild<LLCheckBoxCtrl>("beacons")->setValue(true);
            getChild<LLCheckBoxCtrl>("highlights")->setControlValue(LLSD(false)); // just to be sure it's in sync with llpipeline
            getChild<LLCheckBoxCtrl>("highlights")->setValue(false);
        }
    }
    else if(name == "beacons")
    {
        DXPipeline::toggleRenderBeacons();
        // Don't allow both to be OFF at the same time. Toggle the other one on if both now off.
        if (
            !DXPipeline::getRenderBeacons() &&
            !DXPipeline::getRenderHighlights() )
        {
            DXPipeline::setRenderHighlights(true);
            getChild<LLCheckBoxCtrl>("highlights")->setControlValue(LLSD(true));
            getChild<LLCheckBoxCtrl>("highlights")->setValue(true);
            getChild<LLCheckBoxCtrl>("beacons")->setControlValue(LLSD(false)); // just to be sure it's in sync with llpipeline
            getChild<LLCheckBoxCtrl>("beacons")->setValue(false);
        }
    }
}
