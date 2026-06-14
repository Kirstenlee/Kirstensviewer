/**
 * @file llhudview.cpp
 * @brief 2D HUD overlay
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#include "llhudview.h"

// library includes
#include "v4color.h"
#include "llcoord.h"

// viewer includes
#include "llcallingcard.h"
#include "llviewercontrol.h"
#include "llfloaterworldmap.h"
#include "llworldmapview.h"
#include "lltracker.h"
#include "llviewercamera.h"
#include "llui.h"
#include "lluictrlfactory.h"

LLHUDView *gHUDView = NULL;

LLHUDView::LLHUDView(const LLRect& r)
{
    buildFromFile( "panel_hud.xml");
    setShape(r, true);
}

LLHUDView::~LLHUDView()
{
}

// virtual
void LLHUDView::draw()
{
    LLTracker::drawHUDArrow();

    // Draw framing guides for machinima
    if (gSavedSettings.getBOOL("CameraFramingEnabled"))
    {
        drawFramingGuides();
    }

    LLView::draw();
}

void LLHUDView::drawFramingGuides()
{
    S32 frame_type = gSavedSettings.getS32("CameraFramingType");
    F32 opacity = gSavedSettings.getF32("CameraFramingOpacity");
    F32 scale = gSavedSettings.getF32("CameraFramingScale");
    LLColor4 color = gSavedSettings.getColor4("CameraFramingColor");
    color.mV[VW] = opacity;  // Set alpha from opacity setting

    LLRect screen_rect = getLocalRect();
    F32 width = (F32)screen_rect.getWidth();
    F32 height = (F32)screen_rect.getHeight();

    // Apply scale from center
    F32 center_x = width / 2.0f;
    F32 center_y = height / 2.0f;
    F32 scaled_width = width * scale;
    F32 scaled_height = height * scale;
    F32 offset_x = (width - scaled_width) / 2.0f;
    F32 offset_y = (height - scaled_height) / 2.0f;

    gGL.color4fv(color.mV);
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    // Push matrix and translate/scale for framing
    gGL.pushMatrix();
    gGL.translatef(offset_x, offset_y, 0.0f);

    switch(frame_type)
    {
        case 0: // Rule of Thirds
            drawRuleOfThirds(scaled_width, scaled_height);
            break;
        case 1: // Golden Ratio
            drawGoldenRatio(scaled_width, scaled_height);
            break;
        case 2: // Center Cross
            drawCenterCross(scaled_width, scaled_height);
            break;
        case 3: // Safe Zones
            drawSafeZones(scaled_width, scaled_height);
            break;
        case 4: // Diagonal
            drawDiagonal(scaled_width, scaled_height);
            break;
        case 5: // Triangle
            drawTriangle(scaled_width, scaled_height);
            break;
    }

    gGL.popMatrix();
}

void LLHUDView::drawRuleOfThirds(F32 width, F32 height)
{
    // Vertical lines at 1/3 and 2/3
    F32 third_w = width / 3.0f;
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(third_w, 0.0f);
    gGL.vertex2f(third_w, height);
    gGL.vertex2f(third_w * 2.0f, 0.0f);
    gGL.vertex2f(third_w * 2.0f, height);
    gGL.end();

    // Horizontal lines at 1/3 and 2/3
    F32 third_h = height / 3.0f;
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(0.0f, third_h);
    gGL.vertex2f(width, third_h);
    gGL.vertex2f(0.0f, third_h * 2.0f);
    gGL.vertex2f(width, third_h * 2.0f);
    gGL.end();
}

void LLHUDView::drawGoldenRatio(F32 width, F32 height)
{
    // Golden ratio is approximately 1.618
    F32 golden = 0.618f;  // 1/1.618

    // Vertical lines
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(width * golden, 0.0f);
    gGL.vertex2f(width * golden, height);
    gGL.vertex2f(width * (1.0f - golden), 0.0f);
    gGL.vertex2f(width * (1.0f - golden), height);
    gGL.end();

    // Horizontal lines
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(0.0f, height * golden);
    gGL.vertex2f(width, height * golden);
    gGL.vertex2f(0.0f, height * (1.0f - golden));
    gGL.vertex2f(width, height * (1.0f - golden));
    gGL.end();
}

void LLHUDView::drawCenterCross(F32 width, F32 height)
{
    F32 center_x = width / 2.0f;
    F32 center_y = height / 2.0f;

    // Vertical center line
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(center_x, 0.0f);
    gGL.vertex2f(center_x, height);
    gGL.end();

    // Horizontal center line
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(0.0f, center_y);
    gGL.vertex2f(width, center_y);
    gGL.end();
}

void LLHUDView::drawSafeZones(F32 width, F32 height)
{
    // Action safe zone (90%)
    F32 action_margin_w = width * 0.05f;
    F32 action_margin_h = height * 0.05f;

    gGL.begin(LLRender::LINE_LOOP);
    gGL.vertex2f(action_margin_w, action_margin_h);
    gGL.vertex2f(width - action_margin_w, action_margin_h);
    gGL.vertex2f(width - action_margin_w, height - action_margin_h);
    gGL.vertex2f(action_margin_w, height - action_margin_h);
    gGL.end();

    // Title safe zone (80%)
    F32 title_margin_w = width * 0.1f;
    F32 title_margin_h = height * 0.1f;

    gGL.begin(LLRender::LINE_LOOP);
    gGL.vertex2f(title_margin_w, title_margin_h);
    gGL.vertex2f(width - title_margin_w, title_margin_h);
    gGL.vertex2f(width - title_margin_w, height - title_margin_h);
    gGL.vertex2f(title_margin_w, height - title_margin_h);
    gGL.end();
}

void LLHUDView::drawDiagonal(F32 width, F32 height)
{
    // Diagonal from bottom-left to top-right
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(0.0f, 0.0f);
    gGL.vertex2f(width, height);
    gGL.end();

    // Diagonal from top-left to bottom-right
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(0.0f, height);
    gGL.vertex2f(width, 0.0f);
    gGL.end();
}

void LLHUDView::drawTriangle(F32 width, F32 height)
{
    F32 center_x = width / 2.0f;

    // Triangle composition guide (apex at top center)
    gGL.begin(LLRender::LINE_LOOP);
    gGL.vertex2f(center_x, height);           // Top center
    gGL.vertex2f(0.0f, 0.0f);                 // Bottom left
    gGL.vertex2f(width, 0.0f);                // Bottom right
    gGL.end();

    // Base line emphasis
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(0.0f, height / 3.0f);
    gGL.vertex2f(width, height / 3.0f);
    gGL.end();
}

/*virtual*/
bool LLHUDView::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (LLTracker::handleMouseDown(x, y))
    {
        return true;
    }
    return LLView::handleMouseDown(x, y, mask);
}
