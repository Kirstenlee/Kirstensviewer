/**
 * @file lldrawpoolsimple.cpp
 * @brief LLDrawPoolSimple class implementation
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

#include "lldrawpoolsimple.h"

#include "llviewershadermgr.h"
#include "llrender.h"
#include "pipeline.h"
#ifdef DX_RENDER
#include "dxdrawpoolsimple.h"
#endif

static LLTrace::BlockTimerStatHandle FTM_RENDER_SIMPLE_DEFERRED("Deferred Simple");

// DXDrawPoolSimple (dxdrawpoolsimple.cpp) is the sole, real implementation
// for 4 of these 5 pools. LLDrawPoolSimple::renderDeferred() below is
// deliberately left as its own in-place #ifdef branch rather than moved
// there - it predates the DXDrawPoolSimple split (see dxdrawpoolsimple.h)
// and is grandfathered by that earlier design choice, not an oversight.

void LLDrawPoolGlow::renderPostDeferred(S32 pass)
{
#ifdef DX_RENDER
    DXDrawPoolSimple::renderGlowPostDeferred(*this, pass);
#endif
}

LLDrawPoolSimple::LLDrawPoolSimple() :
    LLRenderPass(POOL_SIMPLE)
{
}

LLDrawPoolAlphaMask::LLDrawPoolAlphaMask() :
    LLRenderPass(POOL_ALPHA_MASK)
{
}

LLDrawPoolFullbrightAlphaMask::LLDrawPoolFullbrightAlphaMask() :
    LLRenderPass(POOL_FULLBRIGHT_ALPHA_MASK)
{
}

//===============================
//DEFERRED IMPLEMENTATION
//===============================

S32 LLDrawPoolSimple::getNumDeferredPasses()
{
    return 1;
}

void LLDrawPoolSimple::renderDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_SIMPLE_DEFERRED);
    LLGLDisable blend(GL_BLEND);

    // Rigged (skinned) geometry needs WEIGHT/WEIGHT4/JOINT vertex
    // attributes, supported via DXVertexLayout's MAP_WEIGHT4. Renders both
    // passes.
    gDeferredDiffuseProgram.bind();
    pushBatches(LLRenderPass::PASS_SIMPLE, true, true);

    gDeferredDiffuseProgram.bind(true);
    pushRiggedBatches(LLRenderPass::PASS_SIMPLE_RIGGED, true, true);
}

static LLTrace::BlockTimerStatHandle FTM_RENDER_ALPHA_MASK_DEFERRED("Deferred Alpha Mask");


void LLDrawPoolAlphaMask::renderDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_ALPHA_MASK_DEFERRED);
#ifdef DX_RENDER
    DXDrawPoolSimple::renderAlphaMaskDeferred(*this, pass);
#endif
}

// grass drawpool
LLDrawPoolGrass::LLDrawPoolGrass() :
 LLRenderPass(POOL_GRASS)
{

}

void LLDrawPoolGrass::renderDeferred(S32 pass)
{
#ifdef DX_RENDER
    DXDrawPoolSimple::renderGrassDeferred(*this, pass);
#endif
}


// Fullbright drawpool
LLDrawPoolFullbright::LLDrawPoolFullbright() :
    LLRenderPass(POOL_FULLBRIGHT)
{
}

void LLDrawPoolFullbright::renderPostDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_FULLBRIGHT);
#ifdef DX_RENDER
    DXDrawPoolSimple::renderFullbrightPostDeferred(*this, pass);
#endif
}

void LLDrawPoolFullbrightAlphaMask::renderPostDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_FULLBRIGHT);
#ifdef DX_RENDER
    DXDrawPoolSimple::renderFullbrightAlphaMaskPostDeferred(*this, pass);
#endif
}

