/**
 * @file llboxprobes.cpp
 * @brief LLBoxProbes implementation.
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
#include "llboxprobes.h"
#include "DXDevice.h"
#include "llmatrix4a.h"

#include <cfloat>

void LLBoxProbes::update(const std::vector<LLReflectionMap*>& maps, U32 max_count, F32 radscale)
{
    max_count = llmin(max_count, (U32)LL_MAX_REFLECTION_PROBE_COUNT);

    // Same depth-bucket scheme as the sphere set: bucket i holds the first box (in depth order) whose
    // depth range covers 1 m slice i. Unset buckets point past the last box, so the shader scan is empty.
    F32 minDepth[256];
    for (int i = 0; i < 256; ++i)
    {
        minDepth[i] = FLT_MAX;
    }

    LLMatrix4a modelview;
    modelview.loadu(gGLModelView);
    LLVector4a oa; // scratch space for transformed origin

    U32 count = 0;
    for (auto* refmap : maps)
    {
        if (refmap == nullptr || count >= max_count)
        {
            break;
        }

        if (refmap->mCubeIndex < 0 || !refmap->isBoxVolume() || !refmap->isRelevant())
        {
            continue;
        }

        refmap->trackViewerObject();

        // Only a finished capture has a cube that matches its box. A probe that is still capturing, or
        // was just moved, is skipped rather than sampled against the wrong geometry.
        if (!refmap->mComplete || !refmap->getCaptureBox(mData.boxRefBox[count]))
        {
            continue;
        }

        modelview.affineTransform(refmap->mCaptureOrigin, oa);
        mData.boxRefOrigin[count].set(oa.getF32ptr());
        mData.boxRefOrigin[count].mV[3] = refmap->mCaptureRadius;

        mData.boxRefIndex[count][0] = refmap->mCubeIndex;
        mData.boxRefIndex[count][1] = -1;
        mData.boxRefIndex[count][2] = -1;
        mData.boxRefIndex[count][3] = 1; // manual probe

        mData.boxRefParams[count].set(0.f, radscale, refmap->mFadeIn, 0.f);

        unsigned int depth_min = llclamp(llfloor(refmap->mMinDepth), 0, 255);
        unsigned int depth_max = llclamp(llfloor(refmap->mMaxDepth), 0, 255);
        for (U32 i = depth_min; i <= depth_max; ++i)
        {
            if (refmap->mMinDepth < minDepth[i])
            {
                minDepth[i] = refmap->mMinDepth;
                mData.boxRefBucket[i][0] = count;
            }
        }

        ++count;
    }

    for (int i = 0; i < 256; ++i)
    {
        if (minDepth[i] == FLT_MAX)
        {
            mData.boxRefBucket[i][0] = count;
        }
        mData.boxRefBucket[i][1] = count;
        mData.boxRefBucket[i][2] = count;
        mData.boxRefBucket[i][3] = count;
    }

    mData.boxCount = count;
    mData.pad[0] = mData.pad[1] = mData.pad[2] = 0;

    upload();
}

void LLBoxProbes::upload()
{
    // Created once, then re-uploaded through DXBuffer::upload() (dynamic buffer).
    if (!mDXUBO.getBuffer())
    {
        mDXUBO.createConstantBuffer(sizeof(Data), &mData);
    }
    else
    {
        mDXUBO.upload(&mData, sizeof(Data));
    }
}

void LLBoxProbes::bind()
{
    // register(b2) in reflectionProbeF.hlsl's cbuffer BoxProbes. Pixel stage only.
    ID3D11Buffer* cb = mDXUBO.getBuffer();
    if (cb)
    {
        gDXDevice.getContext()->PSSetConstantBuffers(2, 1, &cb);
    }
}

void LLBoxProbes::destroy()
{
    mDXUBO.destroy();
}
