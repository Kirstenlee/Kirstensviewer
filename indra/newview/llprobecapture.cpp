/**
 * @file llprobecapture.cpp
 * @brief LLProbeCapture implementation.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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
#include "llprobecapture.h"
#include "llviewershadermgr.h"
#include "pipeline.h"

bool LLProbeCapture::allocate(U32 resolution, U32 mip_count, bool hdr, U32 blur_scale, U32 blur_format)
{
    release();

    if (resolution == 0 || mip_count == 0)
    {
        return false;
    }

    U32 target_res = resolution * blur_scale;
    mBlurTarget.allocate(target_res, target_res, blur_format, true);
    if (!mBlurTarget.isComplete())
    {
        release();
        return false;
    }

    mMips.resize(mip_count);
    U32 res = resolution;
    for (U32 i = 0; i < mip_count; ++i)
    {
        // DX_RENDER uses RGBA16F for HDR and RGBA otherwise. The D3D11 copy path needs a castable format.
        mMips[i].allocate(res, res, hdr ? GL_RGBA16F : GL_RGBA);
        res = llmax(res / 2, (U32)1);
    }

    mResolution = resolution;
    mBlurScale = blur_scale;
    mHDR = hdr;
    return true;
}

void LLProbeCapture::release()
{
    mBlurTarget.release();
    mMips.clear();
    mResolution = 0;
    mBlurScale = SUPER_SAMPLE;
    mHDR = false;
}

void LLProbeCapture::resolveFace(LLRenderTarget& screen_rt, DXCubeArrayTexture& dst, U32 slice)
{
    if (mMips.empty())
    {
        return;
    }

    gDX.matrixMode(gDX.MM_MODELVIEW);
    gDX.pushMatrix();
    gDX.loadIdentity();

    gDX.matrixMode(gDX.MM_PROJECTION);
    gDX.pushMatrix();
    gDX.loadIdentity();

    gDX.flush();

    static LLStaticHashedString resScale("resScale");
    static LLStaticHashedString direction("direction");

    // Separable Gaussian on the super-sampled face, then back into screen_rt.
    {
        gGaussianProgram.bind();
        // One output texel per tap: the blur target is mResolution * mBlurScale texels wide.
        gGaussianProgram.uniform1f(resScale, 1.f / (mResolution * mBlurScale));
        S32 diffuseChannel = gGaussianProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, LLTexUnit::TT_TEXTURE);

        // horizontal
        gGaussianProgram.uniform2f(direction, 1.f, 0.f);
        gDX.getTexUnit(diffuseChannel)->bind(&screen_rt);
        mBlurTarget.bindTarget();
        gPipeline.mScreenTriangleVB->setBuffer();
        gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        mBlurTarget.flush();

        // vertical
        gGaussianProgram.uniform2f(direction, 0.f, 1.f);
        gDX.getTexUnit(diffuseChannel)->bind(&mBlurTarget);
        screen_rt.bindTarget();
        gPipeline.mScreenTriangleVB->setBuffer();
        gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        screen_rt.flush();
    }

    // The mip chain is downsampled from the blurred face. Each level is copied into dst at the
    // same mip level. Mip count is taken from dst, so the chain and the destination always agree.
    S32 dst_mips = (S32)dst.getMipLevels();

    gReflectionMipProgram.bind();
    S32 diffuseChannel = gReflectionMipProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, LLTexUnit::TT_TEXTURE);

    for (S32 i = 0; i < (S32)mMips.size(); ++i)
    {
        mMips[i].bindTarget();
        if (i == 0)
        {
            gDX.getTexUnit(diffuseChannel)->bind(&screen_rt);
        }
        else
        {
            gDX.getTexUnit(diffuseChannel)->bind(&(mMips[i - 1]));
        }

        gReflectionMipProgram.uniform1f(resScale, 1.f / (mResolution * 2));

        gPipeline.mScreenTriangleVB->setBuffer();
        gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        S32 mip = i - ((S32)mMips.size() - dst_mips);
        if (mip >= 0)
        {
            // copySliceFromBoundRenderTarget() reads mMips[i] through OMGetRenderTargets(), so dst is
            // never bound as an SRV during the copy (see DXCubeArrayTexture.h).
            dst.copySliceFromBoundRenderTarget(mip, slice, (UINT)mMips[i].getWidth(), (UINT)mMips[i].getHeight());
        }
        mMips[i].flush();
    }

    gDX.popMatrix();
    gDX.matrixMode(gDX.MM_MODELVIEW);
    gDX.popMatrix();

    gDX.getTexUnit(diffuseChannel)->unbind(LLTexUnit::TT_TEXTURE);
    gReflectionMipProgram.unbind();
}
