/**
 * @file lldrawpoolpbropaque.cpp
 * @brief LLDrawPoolGLTFPBR class implementation
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#include "lldrawpool.h"
#include "lldrawpoolpbropaque.h"
#include "llviewershadermgr.h"
#include "pipeline.h"
#include "gltfscenemanager.h"

LLDrawPoolGLTFPBR::LLDrawPoolGLTFPBR(U32 type) :
    LLRenderPass(type)
{
    if (type == LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK)
    {
        mRenderType = LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK;
    }
    else
    {
        mRenderType = LLPipeline::RENDER_TYPE_PASS_GLTF_PBR;
    }
}

S32 LLDrawPoolGLTFPBR::getNumDeferredPasses()
{
    return 1;
}

void LLDrawPoolGLTFPBR::renderDeferred(S32 pass)
{
    llassert(!LLPipeline::sRenderingHUDs);

#ifdef DX_RENDER
    // S24 (2026-08-02): LL::GLTFSceneManager's own render()/renderOpaque()
    // (imported .glb/.gltf SCENE-FILE assets specifically, a separate
    // rendering mechanism from ordinary PBR-materialed prims/mesh) call
    // glBindBufferBase() - a GLEW-style fn ptr never assigned under
    // DX_RENDER (OpenGL fully delinked) - see project_dxrender_open_issues
    // memory, task "Fix GLTFSceneManager UBO binding". Skipped here,
    // deliberately, the same way dxdrawpoolsimple.cpp already skips the
    // other GLTFSceneManager call site. pushGLTFBatches()/pushGLTFBatch()
    // route through LLFetchedGLTFMaterial::bind() (shader->uniform*()/
    // bindTexture() only - already DX-safe) and the same
    // LLVertexBuffer::drawRange() chokepoint every other converted pool
    // uses, so ordinary PBR-materialed geometry (the vast majority of
    // in-world content on a PBR-only grid) does NOT depend on the blocked
    // GLTFSceneManager path at all.
    // S24 (2026-08-09, task #170 follow-up): rigged (skinned) variant was
    // skipped with a now-stale comment ("DXVertexLayout doesn't support
    // skinned vertex attributes yet") - invalidated by task #168's
    // MAP_WEIGHT4 fix, same gap already found and fixed in the 4 other
    // pools (dxdrawpoolalpha/simple/bump/materials.cpp). This is exactly
    // the pool a modern PBR mesh body/head renders through - found via a
    // user-confirmed "rigged mesh body + rigged mesh head" report.
    // pushRiggedGLTFBatches() is pure shared code (uploadMatrixPalette() +
    // the same drawRange() chokepoint every converted pool already uses),
    // safe to call unconditionally. mRenderType+1 is the same "next enum
    // value is the RIGGED variant" convention already established for
    // PASS_BUMP_RIGGED etc.
    gDeferredPBROpaqueProgram.bind();
    pushGLTFBatches(mRenderType);

    gDeferredPBROpaqueProgram.bind(true);
    pushRiggedGLTFBatches(mRenderType + 1);
    return;
#endif

    if (mRenderType == LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK)
    {
        LL::GLTFSceneManager::instance().renderOpaque();
    }

    gDeferredPBROpaqueProgram.bind();
    pushGLTFBatches(mRenderType);

    LL::GLTFSceneManager::instance().render(true, true);

    gDeferredPBROpaqueProgram.bind(true);
    pushRiggedGLTFBatches(mRenderType + 1);
}

S32 LLDrawPoolGLTFPBR::getNumPostDeferredPasses()
{
    return 1;
}

void LLDrawPoolGLTFPBR::renderPostDeferred(S32 pass)
{
    if (LLPipeline::sRenderingHUDs)
    {
        gHUDPBROpaqueProgram.bind();
        pushGLTFBatches(mRenderType);
    }
    else if (mRenderType == LLPipeline::RENDER_TYPE_PASS_GLTF_PBR) // HACK -- don't render glow except for the non-alpha masked implementation
    {
        gGL.setColorMask(false, true);
        gPBRGlowProgram.bind();
        pushGLTFBatches(LLRenderPass::PASS_GLTF_GLOW);

        gPBRGlowProgram.bind(true);
        pushRiggedGLTFBatches(LLRenderPass::PASS_GLTF_GLOW_RIGGED);
        // S24 3D masking here causes issues with alpha containing testures avoid for now!
        gGL.setColorMask(true, false);
    }
}

