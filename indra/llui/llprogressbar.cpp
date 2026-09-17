/** 
 * @file llprogressbar.cpp
 * @brief LLProgressBar class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "linden_common.h"

#include "llprogressbar.h"

#include "indra_constants.h"
#include "llmath.h"
#include "llgl.h"
#include "llui.h"
#include "llfontgl.h"
#include "lltimer.h"
#include "llglheaders.h"

#include "llfocusmgr.h"
#include "lluictrlfactory.h"
#include "lluiimage.h"
#include "llcontrol.h"
#include "llrender2dutils.h"

static LLDefaultChildRegistry::Register<LLProgressBar> r("progress_bar");

namespace
{
    // S24: same "image * vertex-tint" fix as LLFloater::draw()'s drawFloaterBackgroundImage()
    // (llfloater.cpp) - see its header comment for the full rationale. LLProgressBar draws its
    // bar/fill images directly rather than through LLPanel/LLButton, so it needs its own copy of
    // the shader swap. Reuses the Controls category (RenderUIHueShiftControls) - same bucket as
    // sliders/scrollbars.
    void drawProgressBarImage(LLUIImage* image, const LLRect& rect, const LLColor4& color)
    {
        static LLCachedControl<F32> hue_shift_degrees(*LLUI::getInstance()->mSettingGroups["config"], "RenderUIHueShiftControls", 0.f);
        F32 degrees = hue_shift_degrees;

        if (!LLUI::bindUIEffectsShader(degrees))
        {
            image->draw(rect, color);
            return;
        }

        image->draw(rect, color);

        LLUI::unbindUIEffectsShader();
    }
}

LLProgressBar::Params::Params()
:	image_bar("image_bar"),
	image_fill("image_fill"),
	color_bar("color_bar"),
	color_bg("color_bg")
{}


LLProgressBar::LLProgressBar(const LLProgressBar::Params& p) 
:	LLUICtrl(p),
	mImageBar(p.image_bar),
	mImageFill(p.image_fill),
	mColorBackground(p.color_bg()),
	mColorBar(p.color_bar()),
	mPercentDone(0.f)
{}

LLProgressBar::~LLProgressBar()
{
	gFocusMgr.releaseFocusIfNeeded( this );
}

void LLProgressBar::draw()
{
	static LLTimer timer;
	F32 alpha = getDrawContext().mAlpha;
	
    if (mImageBar) // optional according to parameters
    {
        LLColor4 image_bar_color = mColorBackground.get();
        image_bar_color.setAlpha(alpha);
        drawProgressBarImage(mImageBar, getLocalRect(), image_bar_color);
    }

    if (mImageFill)
    {
        alpha *= 0.5f + 0.5f*0.5f*(1.f + (F32)sin(3.f*timer.getElapsedTimeF32()));
        LLColor4 bar_color = mColorBar.get();
        bar_color.mV[VALPHA] *= alpha; // modulate alpha
        LLRect progress_rect = getLocalRect();
        progress_rect.mRight = ll_round(getRect().getWidth() * (mPercentDone / 100.f));
        drawProgressBarImage(mImageFill, progress_rect, bar_color);
    }
}

void LLProgressBar::setValue(const LLSD& value)
{
	mPercentDone = llclamp((F32)value.asReal(), 0.f, 100.f);
}
