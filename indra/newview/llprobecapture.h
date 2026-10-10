/**
 * @file llprobecapture.h
 * @brief LLProbeCapture - resolves a rendered probe face into a probe cube.
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

#pragma once

#include "llrendertarget.h"
#include "DXCubeArrayTexture.h"

#include <vector>

// Shared capture stage for probe faces (sphere probes and mirrors). A face is rendered
// at 4x probe resolution into the caller's screen target. resolveFace() then runs a
// separable Gaussian at that resolution, builds the mip chain by downsampling, and
// copies each mip into one cube slice. The mip chain is kept so the radiance and
// irradiance generators can read it afterwards.
class LLProbeCapture
{
public:
    // Super-sampling factor between the rendered face and the probe resolution.
    static constexpr U32 SUPER_SAMPLE = 4;

    LLProbeCapture() = default;
    ~LLProbeCapture() { release(); }

    // resolution: probe face size. mip_count: mip levels of the destination cube array.
    // blur_scale: size of the blur target relative to resolution. Sphere probes use
    // SUPER_SAMPLE; mirrors use 1.
    // blur_format: GL-style format of the blur target. Callers pick it, since sphere probes
    // and mirrors have always used different formats.
    bool allocate(U32 resolution, U32 mip_count, bool hdr, U32 blur_scale, U32 blur_format);
    void release();

    bool isAllocated() const { return !mMips.empty(); }

    U32 getResolution() const { return mResolution; }

    // Size the caller renders each face at.
    U32 getSuperSampleResolution() const { return mResolution * mBlurScale; }

    // Super-sampled blur target, sized getSuperSampleResolution().
    LLRenderTarget& getBlurTarget() { return mBlurTarget; }

    // Mip chain, mip 0 at probe resolution, halving each step.
    std::vector<LLRenderTarget>& getMips() { return mMips; }

    // Blurs screen_rt (a rendered face) and downsamples it into the mip chain. Each mip is
    // copied to slice `slice` of dst, at its own mip level. Assumes the face is fully rendered.
    void resolveFace(LLRenderTarget& screen_rt, DXCubeArrayTexture& dst, U32 slice);

private:
    LLRenderTarget mBlurTarget;
    std::vector<LLRenderTarget> mMips;
    U32 mResolution = 0;
    U32 mBlurScale = SUPER_SAMPLE;
    bool mHDR = false;
};
