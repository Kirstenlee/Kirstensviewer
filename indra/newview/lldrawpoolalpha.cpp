/**
 * @file lldrawpoolalpha.cpp
 * @brief LLDrawPoolAlpha class implementation
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

#include "llviewerprecompiledheaders.h"

#include "lldrawpoolalpha.h"

#include "llviewershadermgr.h"

#ifdef DX_RENDER
#include "dxdrawpoolalpha.h"
#endif

// S24 (2026-09-05, task #291): this used to carry a full GL-era
// implementation (forwardRender()/renderAlpha()/renderDebugAlpha()/
// renderAlphaHighlight(), the emissive helpers, TexSetup()/
// RestoreTexSetup(), and every shader-pointer/blend-factor member that
// existed only to support them - ~750 lines) behind renderPostDeferred()'s
// existing #ifdef DX_RENDER redirect. Confirmed via full-tree grep that
// NONE of it had any caller outside this file, and renderPostDeferred() -
// the only real entry point (LLDrawPool::render() is a base-class no-op
// for this pool, deferred-only) - already returned before ever reaching it
// under DX_RENDER, making it permanently unreachable in the shipped build.
// It's also now doubly dead: task #300's full GL shader-source removal
// (r3712) deleted the .glsl files this body would need, so even a
// hypothetical GL build could no longer run it. DXDrawPoolAlpha
// (dxdrawpoolalpha.cpp) is the sole, real implementation now - this file
// is just the LLDrawPool-required scaffolding plus the two functions that
// bridge to it (prerender(), renderPostDeferred()).

bool LLDrawPoolAlpha::sShowDebugAlpha = false;

LLVector4 LLDrawPoolAlpha::sWaterPlane;

LLDrawPoolAlpha::LLDrawPoolAlpha(U32 type) :
        LLRenderPass(type)
{

}

LLDrawPoolAlpha::~LLDrawPoolAlpha()
{
}

void LLDrawPoolAlpha::prerender()
{
    mShaderLevel = LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

S32 LLDrawPoolAlpha::getNumPostDeferredPasses()
{
    return 1;
}

void LLDrawPoolAlpha::renderPostDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

#ifdef DX_RENDER
    DXDrawPoolAlpha::renderPostDeferred(*this, pass);
#endif
}
