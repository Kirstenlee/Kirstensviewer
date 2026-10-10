/**
 * @file llprobecube.cpp
 * @brief LLProbeCubeStore implementation.
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
#include "llprobecube.h"
#include "llhlslshader.h"
#include "llviewercontrol.h"

bool LLProbeCubeStore::allocate(U32 resolution, U32 count, bool hdr)
{
    release();

    if (resolution == 0 || count == 0)
    {
        return false;
    }

    // Radiance: count + 2 cubes with the full mip chain. Irradiance: count cubes, no mips.
    if (!mRadiance.create(resolution, resolution, count + 2, hdr, true))
    {
        release();
        return false;
    }

    if (!mIrradiance.create(IRRADIANCE_RESOLUTION, IRRADIANCE_RESOLUTION, count, hdr, false))
    {
        release();
        return false;
    }

    mResolution = resolution;
    mCount = count;
    mHDR = hdr;
    return true;
}

void LLProbeCubeStore::release()
{
    mRadiance.destroy();
    mIrradiance.destroy();
    mResolution = 0;
    mCount = 0;
    mHDR = false;
}

U64 LLProbeCubeStore::memoryBytes() const
{
    if (!isAllocated())
    {
        return 0;
    }

    U64 bytes_per_texel = mHDR ? 8 : 4;
    // Radiance: count + 2 cubes, each with its mip chain (about 4/3 of the base level).
    U64 radiance = U64(mCount + 2) * 6 * mResolution * mResolution * 4 / 3 * bytes_per_texel;
    // Irradiance: count cubes, no mips.
    U64 irradiance = U64(mCount) * 6 * IRRADIANCE_RESOLUTION * IRRADIANCE_RESOLUTION * bytes_per_texel;
    return radiance + irradiance;
}

void LLProbeCubeStore::setGeneratorConvention(LLHLSLShader& shader)
{
    static LLCachedControl<bool> swap_xy(gSavedSettings, "RenderCubeConvSwapXY", false);
    static LLCachedControl<bool> flip_x(gSavedSettings, "RenderCubeConvFlipX", false);
    static LLCachedControl<bool> flip_y(gSavedSettings, "RenderCubeConvFlipY", false);

    static LLStaticHashedString sSwapXY("conv_swap_xy");
    static LLStaticHashedString sFlipX("conv_flip_x");
    static LLStaticHashedString sFlipY("conv_flip_y");

    shader.uniform1i(sSwapXY, swap_xy ? 1 : 0);
    shader.uniform1i(sFlipX, flip_x ? 1 : 0);
    shader.uniform1i(sFlipY, flip_y ? 1 : 0);
}
