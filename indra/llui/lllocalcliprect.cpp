/**
* @file lllocalcliprect.cpp
*
* $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#include "lllocalcliprect.h"

#include "llfontgl.h"
#include "llui.h"

#ifdef DX_RENDER
#include "DXDevice.h"
#include "DXUIBatch.h"
#endif

/*static*/ std::stack<LLRect> LLScreenClipRect::sClipRectStack;


LLScreenClipRect::LLScreenClipRect(const LLRect& rect, bool enabled)
:   mScissorState(GL_SCISSOR_TEST),
    mEnabled(enabled)
{
    if (mEnabled)
    {
        pushClipRect(rect);
        mScissorState.setEnabled(!sClipRectStack.empty());
        updateScissorRegion();
    }
}

LLScreenClipRect::~LLScreenClipRect()
{
    if (mEnabled)
    {
        popClipRect();
        updateScissorRegion();
    }
}

//static
void LLScreenClipRect::pushClipRect(const LLRect& rect)
{
    LLRect combined_clip_rect = rect;
    if (!sClipRectStack.empty())
    {
        LLRect top = sClipRectStack.top();
        combined_clip_rect.intersectWith(top);

        if(combined_clip_rect.isEmpty())
        {
            // avoid artifacts where zero area rects show up as lines
            combined_clip_rect = LLRect::null;
        }
    }
    sClipRectStack.push(combined_clip_rect);
}

//static
void LLScreenClipRect::popClipRect()
{
    sClipRectStack.pop();
}

//static
void LLScreenClipRect::updateScissorRegion()
{
    if (sClipRectStack.empty()) return;

    // finish any deferred calls in the old clipping region
    gDX.flush();
#ifdef DX_RENDER
    // S24: also flush gDXUIBatch's separate pending queue, not just LLRender's -
    // see DXUIBatch.h's top comment.
    gDXUIBatch.flushPending();
#endif

    LLRect rect = sClipRectStack.top();
    S32 x,y,w,h;
    x = llfloor(rect.mLeft * LLUI::getScaleFactor().mV[VX]);
    y = llfloor(rect.mBottom * LLUI::getScaleFactor().mV[VY]);
    w = llmax(0, llceil(rect.getWidth() * LLUI::getScaleFactor().mV[VX])) + 1;
    h = llmax(0, llceil(rect.getHeight() * LLUI::getScaleFactor().mV[VY])) + 1;
#ifndef DX_RENDER
    glScissor( x,y,w,h );
#else
    // S24: GL's glScissor(x,y,w,h) is bottom-left-origin; D3D11_RECT is
    // top-left-origin, so y needs flipping - same convention mismatch as
    // DXPipeline::setPresentViewport() (dxpipeline.cpp). Window height is read
    // back off the currently-bound D3D11 viewport rather than pulling in a new
    // llui->newview dependency (gViewerWindow) just to get it.
    ID3D11DeviceContext* ctx = gDXDevice.getContext();
    if (ctx)
    {
        UINT num_vp = 1;
        D3D11_VIEWPORT vp = {};
        ctx->RSGetViewports(&num_vp, &vp);
        if (num_vp > 0 && vp.Height > 0.0f)
        {
            D3D11_RECT scissor = {};
            scissor.left = (LONG)vp.TopLeftX + x;
            scissor.right = (LONG)vp.TopLeftX + x + w;
            scissor.top = (LONG)(vp.TopLeftY + (vp.Height - (float)(y + h)));
            scissor.bottom = (LONG)(vp.TopLeftY + (vp.Height - (float)y));
            ctx->RSSetScissorRects(1, &scissor);
        }
    }
#endif
}

//---------------------------------------------------------------------------
// LLLocalClipRect
//---------------------------------------------------------------------------
LLLocalClipRect::LLLocalClipRect(const LLRect& rect, bool enabled /* = true */)
:   LLScreenClipRect(LLRect(rect.mLeft + LLFontGL::sCurOrigin.mX,
                    rect.mTop + LLFontGL::sCurOrigin.mY,
                    rect.mRight + LLFontGL::sCurOrigin.mX,
                    rect.mBottom + LLFontGL::sCurOrigin.mY), enabled)
{}

LLLocalClipRect::~LLLocalClipRect()
{}
