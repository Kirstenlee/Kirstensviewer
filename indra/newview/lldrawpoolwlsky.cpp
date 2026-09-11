/**
 * @file lldrawpoolwlsky.cpp
 * @brief LLDrawPoolWLSky class implementation
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "lldrawpoolwlsky.h"

#ifdef DX_RENDER
#include "dxdrawpoolwlsky.h"
#endif

// S24 (2026-09-05, task #292): this used to carry a full GL-era
// implementation (renderDome()/renderSkyHazeDeferred()/
// renderSkyCloudsDeferred()/renderStarsDeferred()/renderHeavenlyBodies(),
// the use_hdri_sky() helper, and the sky_shader/cloud_shader/sun_shader/
// moon_shader statics that existed only to support them - ~500 lines)
// behind renderDeferred()'s (and beginDeferredPass()'s/endDeferredPass()'s
// own, separate) existing #ifdef DX_RENDER redirects. Confirmed via
// full-tree grep that none of it had any caller outside this file - the
// only real external dependents are cleanupGL()/restoreGL() and the three
// vertex-data-mask constants (llvowlsky.cpp), all kept below. Doubly dead
// besides: task #300's full GL shader-source removal already deleted the
// .glsl files this body would need, so even a hypothetical GL build could
// no longer run it. DXDrawPoolWLSky (dxdrawpoolwlsky.cpp) is the sole,
// real implementation now - this file is just the LLDrawPool-required
// scaffolding plus the redirects that bridge to it.

LLDrawPoolWLSky::LLDrawPoolWLSky(void) :
    LLDrawPool(POOL_WL_SKY)
{
}

LLDrawPoolWLSky::~LLDrawPoolWLSky()
{
}

LLViewerTexture *LLDrawPoolWLSky::getDebugTexture()
{
    return NULL;
}

void LLDrawPoolWLSky::beginDeferredPass(S32 pass)
{
#ifdef DX_RENDER
    DXDrawPoolWLSky::beginDeferredPass(*this, pass);
#endif
}

void LLDrawPoolWLSky::endDeferredPass(S32 pass)
{
#ifdef DX_RENDER
    DXDrawPoolWLSky::endDeferredPass(*this, pass);
#endif
}

void LLDrawPoolWLSky::renderDeferred(S32 pass)
{
#ifdef DX_RENDER
    DXDrawPoolWLSky::renderDeferred(*this, pass);
#endif
}

LLViewerTexture* LLDrawPoolWLSky::getTexture()
{
    return NULL;
}

void LLDrawPoolWLSky::resetDrawOrders()
{
}

//static
void LLDrawPoolWLSky::cleanupGL()
{
}

//static
void LLDrawPoolWLSky::restoreGL()
{
}
