/**
 * @file lldrawpoolterrain.cpp
 * @brief LLDrawPoolTerrain class implementation
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

#include "lldrawpoolterrain.h"

#include "llfasttimer.h"

#include "llagent.h"
#include "llviewercontrol.h"
#include "lldrawable.h"
#include "llface.h"
#include "llsky.h"
#include "llsurface.h"
#include "llsurfacepatch.h"
#include "llviewerregion.h"
#include "llvlcomposition.h"
#include "llviewerparcelmgr.h"      // for gRenderParcelOwnership
#include "llviewerparceloverlay.h"
#include "llvosurfacepatch.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h" // To get alpha gradients
#include "llworld.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llrender.h"
#include "llenvironment.h"
#include "llsettingsvo.h"
#include "dxdrawpoolterrain.h"

const F32 DETAIL_SCALE = 1.f/16.f;
int DebugDetailMap = 0;

S32 LLDrawPoolTerrain::sPBRDetailMode = 0;
F32 LLDrawPoolTerrain::sDetailScale = DETAIL_SCALE;
F32 LLDrawPoolTerrain::sPBRDetailScale = DETAIL_SCALE;
static LLTrace::BlockTimerStatHandle FTM_SHADOW_TERRAIN("Terrain Shadow");


LLDrawPoolTerrain::LLDrawPoolTerrain(LLViewerTexture *texturep) :
    LLFacePool(POOL_TERRAIN),
    mTexturep(texturep)
{
    // Hack!
    sDetailScale = 1.f/gSavedSettings.getF32("RenderTerrainScale");
    sPBRDetailScale = 1.f/gSavedSettings.getF32("RenderTerrainPBRScale");
    sPBRDetailMode = gSavedSettings.getS32("RenderTerrainPBRDetail");
    mAlphaRampImagep = LLViewerTextureManager::getFetchedTexture(IMG_ALPHA_GRAD);

    //gDX.getTexUnit(0)->bind(mAlphaRampImagep.get());
    mAlphaRampImagep->setAddressMode(LLTexUnit::TAM_CLAMP);

    m2DAlphaRampImagep = LLViewerTextureManager::getFetchedTexture(IMG_ALPHA_GRAD_2D);

    //gDX.getTexUnit(0)->bind(m2DAlphaRampImagep.get());
    m2DAlphaRampImagep->setAddressMode(LLTexUnit::TAM_CLAMP);

    mTexturep->setBoostLevel(LLGLTexture::BOOST_TERRAIN);

    //gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
}

LLDrawPoolTerrain::~LLDrawPoolTerrain()
{
    llassert( gPipeline.findPool( getType(), getTexture() ) == NULL );
}

U32 LLDrawPoolTerrain::getVertexDataMask()
{
    if (LLPipeline::sShadowRender)
    {
        return LLVertexBuffer::MAP_VERTEX;
    }
    else if (LLHLSLShader::sCurBoundShaderPtr)
    {
        return VERTEX_DATA_MASK & ~(LLVertexBuffer::MAP_TEXCOORD2 | LLVertexBuffer::MAP_TEXCOORD3);
    }
    else
    {
        return VERTEX_DATA_MASK;
    }
}

void LLDrawPoolTerrain::prerender()
{
    static LLCachedControl<S32> render_terrain_pbr_detail(gSavedSettings, "RenderTerrainPBRDetail");
    sPBRDetailMode = render_terrain_pbr_detail;
}

void LLDrawPoolTerrain::beginDeferredPass(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    DXDrawPoolTerrain::beginDeferredPass(*this, pass);
}

void LLDrawPoolTerrain::endDeferredPass(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    DXDrawPoolTerrain::endDeferredPass(*this, pass);
}

void LLDrawPoolTerrain::renderDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    DXDrawPoolTerrain::renderDeferred(*this, pass);
}

void LLDrawPoolTerrain::beginShadowPass(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_SHADOW_TERRAIN);
    LLFacePool::beginRenderPass(pass);
    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gDeferredShadowProgram.bind();

    LLEnvironment& environment = LLEnvironment::instance();
    gDeferredShadowProgram.uniform1i(LLShaderMgr::SUN_UP_FACTOR, environment.getIsSunUp() ? 1 : 0);
}

void LLDrawPoolTerrain::endShadowPass(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_SHADOW_TERRAIN);
    LLFacePool::endRenderPass(pass);
    gDeferredShadowProgram.unbind();
}

void LLDrawPoolTerrain::renderShadow(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_SHADOW_TERRAIN);
    if (mDrawFace.empty())
    {
        return;
    }
    //LLGLEnable offset(GL_POLYGON_OFFSET);
    //glCullFace(GL_FRONT);
    drawLoop();
    //glCullFace(GL_BACK);
}


void LLDrawPoolTerrain::drawLoop()
{
    if (!mDrawFace.empty())
    {
        for (std::vector<LLFace*>::iterator iter = mDrawFace.begin();
             iter != mDrawFace.end(); iter++)
        {
            LLFace *facep = *iter;

            llassert(gDX.getMatrixMode() == LLRender::MM_MODELVIEW);
            LLRenderPass::applyModelMatrix(&facep->getDrawable()->getRegion()->mRenderMatrix);

            facep->renderIndexed();
        }
    }
}

// renderFullShader()/renderFullShaderTextures()/renderFullShaderPBR()/hilightParcelOwners()/
// renderOwnership() and the already-dead-in-both-builds renderFull2TU()/renderFull4TU()/
// renderSimple() (legacy fixed-function multitexture, superseded before DX_RENDER even
// existed) were removed here - LLDrawPoolTerrain::renderDeferred() redirects unconditionally to
// DXDrawPoolTerrain::renderDeferred(), which is a fully independent implementation (its own
// namespace-local renderFullShader()/renderFullShaderTextures()/renderFullShaderPBR()/
// renderOwnership()/hilightParcelOwners() in dxdrawpoolterrain.cpp) - none of the functions
// removed here had any other caller. drawLoop() stays: LLDrawPoolTerrain::renderShadow() (no
// DX_RENDER redirect - shadow pass is genuinely shared) still calls it directly.


void LLDrawPoolTerrain::dirtyTextures(const std::set<LLViewerFetchedTexture*>& textures)
{
    LLViewerFetchedTexture* tex = LLViewerTextureManager::staticCastToFetchedTexture(mTexturep) ;
    if (tex && textures.find(tex) != textures.end())
    {
        for (std::vector<LLFace*>::iterator iter = mReferences.begin();
             iter != mReferences.end(); iter++)
        {
            LLFace *facep = *iter;
            gPipeline.markTextured(facep->getDrawable());
        }
    }
}

LLViewerTexture *LLDrawPoolTerrain::getTexture()
{
    return mTexturep;
}

LLViewerTexture *LLDrawPoolTerrain::getDebugTexture()
{
    return mTexturep;
}


LLColor3 LLDrawPoolTerrain::getDebugColor() const
{
    return LLColor3(0.f, 0.f, 1.f);
}
