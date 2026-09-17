/**
 * @file lldrawpool.cpp
 * @brief LLDrawPoolMaterials class implementation
 * @author Jonathan "Geenz" Goodman
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013, Linden Research, Inc.
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

#include "lldrawpoolmaterials.h"
#include "llviewershadermgr.h"
#include "pipeline.h"
#ifdef DX_RENDER
#include "dxdrawpoolmaterials.h"
#endif

LLDrawPoolMaterials::LLDrawPoolMaterials()
:  LLRenderPass(LLDrawPool::POOL_MATERIALS)
{

}

void LLDrawPoolMaterials::prerender()
{
    mShaderLevel = LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

S32 LLDrawPoolMaterials::getNumDeferredPasses()
{
    // 12 render passes times 2 (one for each rigged and non rigged)
    return 12*2;
}

void LLDrawPoolMaterials::beginDeferredPass(S32 pass)
{

    bool rigged = false;
    if (pass >= 12)
    {
        rigged = true;
        pass -= 12;
    }
    U32 shader_idx[] =
    {
        0, //LLRenderPass::PASS_MATERIAL,
        //1, //LLRenderPass::PASS_MATERIAL_ALPHA,
        2, //LLRenderPass::PASS_MATERIAL_ALPHA_MASK,
        3, //LLRenderPass::PASS_MATERIAL_ALPHA_GLOW,
        4, //LLRenderPass::PASS_SPECMAP,
        //5, //LLRenderPass::PASS_SPECMAP_BLEND,
        6, //LLRenderPass::PASS_SPECMAP_MASK,
        7, //LLRenderPass::PASS_SPECMAP_GLOW,
        8, //LLRenderPass::PASS_NORMMAP,
        //9, //LLRenderPass::PASS_NORMMAP_BLEND,
        10, //LLRenderPass::PASS_NORMMAP_MASK,
        11, //LLRenderPass::PASS_NORMMAP_GLOW,
        12, //LLRenderPass::PASS_NORMSPEC,
        //13, //LLRenderPass::PASS_NORMSPEC_BLEND,
        14, //LLRenderPass::PASS_NORMSPEC_MASK,
        15, //LLRenderPass::PASS_NORMSPEC_GLOW,
    };

    U32 idx = shader_idx[pass];

    mShader = &(gDeferredMaterialProgram[idx]);

    if (rigged)
    {
        llassert(mShader->mRiggedVariant != nullptr);
        mShader = mShader->mRiggedVariant;
    }

    gPipeline.bindDeferredShader(*mShader);
}

void LLDrawPoolMaterials::endDeferredPass(S32 pass)
{

    mShader->unbind();

    LLRenderPass::endRenderPass(pass);
}

void LLDrawPoolMaterials::renderDeferred(S32 pass)
{
#ifdef DX_RENDER
    DXDrawPoolMaterials::renderDeferred(*this, pass);
#endif
}
