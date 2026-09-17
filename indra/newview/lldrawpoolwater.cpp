/**
 * @file lldrawpoolwater.cpp
 * @brief LLDrawPoolWater class implementation
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
#include "lldrawpoolwater.h"

#include "DXCubeMap.h"
#include "llface.h"
#include "llviewertexturelist.h"
#include "llvowater.h"
#include "llviewershadermgr.h"
#include "llenvironment.h"
#include "llsettingswater.h"

#ifdef DX_RENDER
#include "dxdrawpoolwater.h"
#endif

LLTrace::BlockTimerStatHandle FTM_RENDER_WATER_OPAQUE("Water Opaque");

bool LLDrawPoolWater::sSkipScreenCopy = false;
bool LLDrawPoolWater::sNeedsReflectionUpdate = true;
bool LLDrawPoolWater::sNeedsDistortionUpdate = true;
F32 LLDrawPoolWater::sWaterFogEnd = 0.f;

LLDrawPoolWater::LLDrawPoolWater() : LLFacePool(POOL_WATER)
{
}

LLDrawPoolWater::~LLDrawPoolWater()
{
}

void LLDrawPoolWater::setTransparentTextures(const LLUUID& transparentTextureId, const LLUUID& nextTransparentTextureId)
{
    LLSettingsWater::ptr_t pwater = LLEnvironment::instance().getCurrentWater();
    mWaterImagep[0] = LLViewerTextureManager::getFetchedTexture(!transparentTextureId.isNull() ? transparentTextureId : pwater->GetDefaultTransparentTextureAssetId());
    mWaterImagep[1] = LLViewerTextureManager::getFetchedTexture(!nextTransparentTextureId.isNull() ? nextTransparentTextureId : (!transparentTextureId.isNull() ? transparentTextureId : pwater->GetDefaultTransparentTextureAssetId()));
    // S24 BUG FIX: Water textures must have high priority - they're always visible and critical
    // Without boost, water gets stuck in queue after heavy texture load (e.g., post-TP)
    mWaterImagep[0]->setBoostLevel(LLGLTexture::BOOST_HIGH);
    mWaterImagep[1]->setBoostLevel(LLGLTexture::BOOST_HIGH);
    mWaterImagep[0]->addTextureStats(2048.f*2048.f);  // Increased from 1024x1024
    mWaterImagep[1]->addTextureStats(2048.f*2048.f);
}

void LLDrawPoolWater::setOpaqueTexture(const LLUUID& opaqueTextureId)
{
    LLSettingsWater::ptr_t pwater = LLEnvironment::instance().getCurrentWater();
    mOpaqueWaterImagep = LLViewerTextureManager::getFetchedTexture(opaqueTextureId);
    // S24 BUG FIX: Opaque water texture also needs high priority
    mOpaqueWaterImagep->setBoostLevel(LLGLTexture::BOOST_HIGH);
    mOpaqueWaterImagep->addTextureStats(2048.f*2048.f);  // Increased from 1024x1024
}

void LLDrawPoolWater::setNormalMaps(const LLUUID& normalMapId, const LLUUID& nextNormalMapId)
{
    LLSettingsWater::ptr_t pwater = LLEnvironment::instance().getCurrentWater();
    mWaterNormp[0] = LLViewerTextureManager::getFetchedTexture(!normalMapId.isNull() ? normalMapId : pwater->GetDefaultWaterNormalAssetId());
    mWaterNormp[1] = LLViewerTextureManager::getFetchedTexture(!nextNormalMapId.isNull() ? nextNormalMapId : (!normalMapId.isNull() ? normalMapId : pwater->GetDefaultWaterNormalAssetId()));
    // S24 BUG FIX: Water normal maps CRITICAL - without them water appears flat/wrong
    // This was causing "stuck water texture" after TP - normals were deprioritized
    mWaterNormp[0]->setBoostLevel(LLGLTexture::BOOST_SUPER_HIGH);  // Super high - always needed
    mWaterNormp[1]->setBoostLevel(LLGLTexture::BOOST_SUPER_HIGH);
    mWaterNormp[0]->addTextureStats(2048.f*2048.f);  // Increased from 1024x1024
    mWaterNormp[1]->addTextureStats(2048.f*2048.f);
}

void LLDrawPoolWater::prerender()
{
    mShaderLevel = DXCubeMap::sUseCubeMaps ? LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_WATER) : 0;
}

S32 LLDrawPoolWater::getNumPostDeferredPasses()
{
    if (LLViewerCamera::getInstance()->getOrigin().mV[2] < 1024.f)
    {
        return 1;
    }

    return 0;
}

void LLDrawPoolWater::beginPostDeferredPass(S32 pass)
{
#ifdef DX_RENDER
    DXDrawPoolWater::beginPostDeferredPass(*this, pass);
#endif
}

void LLDrawPoolWater::renderPostDeferred(S32 pass)
{
#ifdef DX_RENDER
    DXDrawPoolWater::renderPostDeferred(*this, pass);
#endif
}

void LLDrawPoolWater::pushWaterPlanes(int pass)
{
    LLVOWater* water = nullptr;
    for (LLFace* const& face : mDrawFace)
    {
        water = static_cast<LLVOWater*>(face->getViewerObject());

        face->renderIndexed();

        // Note non-void water being drawn, updates required
        // Previously we had some logic to determine if this pass was also our water edge pass.
        // Now we only have one pass.  Check if we're doing a region water plane or void water plane.
        // - Geenz 2025-02-11
        if (!water->getIsEdgePatch())
        {
            sNeedsReflectionUpdate = true;
            sNeedsDistortionUpdate = true;
        }
    }
}

LLViewerTexture *LLDrawPoolWater::getDebugTexture()
{
    return LLViewerTextureManager::getFetchedTexture(IMG_SMOKE);
}

LLColor3 LLDrawPoolWater::getDebugColor() const
{
    return LLColor3(0.f, 1.f, 1.f);
}
