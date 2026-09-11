/**
 * @file dxdrawpoolmaterials.cpp
 * @brief Fresh DX11-native implementation of LLDrawPoolMaterials::renderDeferred()
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "llviewerprecompiledheaders.h"

#include "dxdrawpoolmaterials.h"

#include "lldrawpoolmaterials.h"
#include "pipeline.h"
#include "llrender.h"
#include "llspatialpartition.h"
#include "llhlslshader.h"
#include "llshadermgr.h"

// static
void DXDrawPoolMaterials::renderDeferred(LLDrawPoolMaterials& pool, S32 pass)
{
    (void)pool; // unused - mShader is private to LLDrawPoolMaterials; the shader is reached via
                // LLHLSLShader::sCurBoundShaderPtr instead (already bound by beginDeferredPass()'s
                // gPipeline.bindDeferredShader() call, which calls shader.bind() at its own top -
                // same "use whatever's currently bound" pattern already established for DXUIBatch/
                // LLSelectNode::renderOneSilhouette()).

    static const U32 type_list[] =
    {
        LLRenderPass::PASS_MATERIAL,
        LLRenderPass::PASS_MATERIAL_ALPHA_MASK,
        LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE,
        LLRenderPass::PASS_SPECMAP,
        LLRenderPass::PASS_SPECMAP_MASK,
        LLRenderPass::PASS_SPECMAP_EMISSIVE,
        LLRenderPass::PASS_NORMMAP,
        LLRenderPass::PASS_NORMMAP_MASK,
        LLRenderPass::PASS_NORMMAP_EMISSIVE,
        LLRenderPass::PASS_NORMSPEC,
        LLRenderPass::PASS_NORMSPEC_MASK,
        LLRenderPass::PASS_NORMSPEC_EMISSIVE,
    };

    // S24 (2026-08-09, task #170): rigged materials batches were skipped
    // entirely until task #168 fixed DXVertexLayout's MAP_WEIGHT4 rejection.
    // beginDeferredPass() (lldrawpoolmaterials.cpp) is already shared,
    // backend-agnostic code - it unconditionally selects mShader's rigged
    // variant and calls bindDeferredShader() before this function ever
    // runs, for pass>=12, on both backends already - so
    // LLHLSLShader::sCurBoundShaderPtr (used as `shader` below) is already
    // correctly the rigged variant here, no extra shader lookup needed.
    bool rigged = false;
    if (pass >= 12)
    {
        rigged = true;
        pass -= 12;
    }

    llassert(pass < (S32)(sizeof(type_list) / sizeof(U32)));
    U32 type = type_list[pass];
    if (rigged)
    {
        type += 1; // PASS_X_RIGGED is always PASS_X's next enum value
    }

    LLCullResult::drawinfo_iterator begin = gPipeline.beginRenderMap(type);
    LLCullResult::drawinfo_iterator end = gPipeline.endRenderMap(type);

    // S24 (2026-08-09, task #134): real fix - was diffuse-only (see the removed
    // class-comment gap notice in dxdrawpoolmaterials.h). enableTexture()/
    // uniform1f(U32,...)/uniform4fv(U32,...) are all already confirmed DX-safe
    // (enableTexture(): resolves mTexture[] regardless of mProgramObject, task
    // #62-era work; uniform1f/4fv(U32,...): resolve LLShaderMgr::mReservedUniforms[index]
    // by name and push through DXShader::setUniformFloatArray(), task #107) -
    // this pool just never called them. Mirrors LLDrawPoolMaterials::renderDeferred()'s
    // GL body exactly, except: (1) skips the GL-only getUniformLocation()+raw
    // glUniform1f()/glUniform4fv() calls entirely - raw glUniform*() are
    // unguarded null function pointers under DX_RENDER, and getUniformLocation()
    // gates on mProgramObject (always 0 here, same class of bug as task #60's
    // isComplete() fix) - the uniformNf(U32,...) member overloads used here
    // don't need a location precheck at all, they no-op safely by name if a
    // given shader variant doesn't declare that uniform; (2) no mValue-based
    // redundant-set avoidance (DX_RENDER's uniformNf() doesn't consult that
    // cache), so the per-batch change-tracking below exists for the same
    // "avoid redundant driver calls" reason GL has it, not for correctness.
    LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
    if (!shader)
    {
        LL_WARNS_ONCE("DXDrawPool") << "DXDrawPoolMaterials::renderDeferred() with no bound shader - dropping batch." << LL_ENDL;
        return;
    }

    S32 diffuseChannel = shader->enableTexture(LLShaderMgr::DIFFUSE_MAP);
    S32 specChannel = shader->enableTexture(LLShaderMgr::SPECULAR_MAP);
    S32 normChannel = shader->enableTexture(LLShaderMgr::BUMP_MAP);

    LLTexture* lastDiffuse = nullptr;
    LLTexture* lastSpecMap = nullptr;
    LLTexture* lastNormalMap = nullptr;
    // Sentinels outside each field's real range - forces a real uniform set
    // on the very first draw item regardless of its actual value.
    F32 lastIntensity = -1.f;
    F32 lastFullbright = -1.f;
    F32 lastMinimumAlpha = -1.f;
    LLVector4 lastSpecular(-1.f, -1.f, -1.f, -1.f);

    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        if (params.mSpecColor != lastSpecular)
        {
            lastSpecular = params.mSpecColor;
            shader->uniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, lastSpecular.mV);
        }

        if (lastIntensity != params.mEnvIntensity)
        {
            lastIntensity = params.mEnvIntensity;
            shader->uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, lastIntensity);
        }

        if (lastMinimumAlpha != params.mAlphaMaskCutoff)
        {
            lastMinimumAlpha = params.mAlphaMaskCutoff;
            shader->uniform1f(LLShaderMgr::MINIMUM_ALPHA, lastMinimumAlpha);
        }

        F32 fullbright = params.mFullbright ? 1.f : 0.f;
        if (lastFullbright != fullbright)
        {
            lastFullbright = fullbright;
            shader->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, lastFullbright);
        }

        if (normChannel > -1 && params.mNormalMap != lastNormalMap)
        {
            lastNormalMap = params.mNormalMap;
            llassert(lastNormalMap);
            gDX.getTexUnit(normChannel)->bindFast(lastNormalMap);
        }

        if (specChannel > -1 && params.mSpecularMap != lastSpecMap)
        {
            lastSpecMap = params.mSpecularMap;
            llassert(lastSpecMap);
            gDX.getTexUnit(specChannel)->bindFast(lastSpecMap);
        }

        if (params.mTexture != lastDiffuse)
        {
            lastDiffuse = params.mTexture;
            if (lastDiffuse)
            {
                gDX.getTexUnit(diffuseChannel)->bindFast(lastDiffuse);
            }
            else
            {
                gDX.getTexUnit(diffuseChannel)->unbindFast(LLTexUnit::TT_TEXTURE);
            }
        }

        // S24 (2026-08-09, task #170): upload matrix palette to shader -
        // was never reached (rigged batches were skipped entirely before
        // task #168).
        if (rigged)
        {
            if (!LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                continue;
            }
        }

        LLRenderPass::applyModelMatrix(params);

        bool tex_setup = false;
        if (params.mTextureMatrix)
        {
            gDX.getTexUnit(0)->activate();
            gDX.matrixMode(LLRender::MM_TEXTURE);
            gDX.loadMatrix((F32*)params.mTextureMatrix->mMatrix);
            gPipeline.mTextureMatrixOps++;
            tex_setup = true;
        }

        params.mVertexBuffer->setBuffer();
        params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);

        if (tex_setup)
        {
            gDX.getTexUnit(0)->activate();
            gDX.loadIdentity();
            gDX.matrixMode(LLRender::MM_MODELVIEW);
        }
    }
}
