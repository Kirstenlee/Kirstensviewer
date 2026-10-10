/**
 * @file llprobecube.h
 * @brief LLProbeCubeStore - cube storage shared by every probe set.
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

#include "DXCubeArrayTexture.h"

class LLHLSLShader;

// Storage for probe cubes. It owns the D3D11 arrays and nothing else: no scheduling,
// no placement, no uniforms. Slot layout:
//   radiance:   [0, count)  probe radiance cubes
//               [count]     scratch cube for sphere probe capture
//               [count+1]   scratch cube for the realtime probe
//   irradiance: [0, count)  probe irradiance cubes
//
// Face slice = cube * 6 + face, with D3D11 face order (see cubeFaceConvention.hlsli).
class LLProbeCubeStore
{
public:
    static constexpr U32 IRRADIANCE_RESOLUTION = 16;

    LLProbeCubeStore() = default;
    ~LLProbeCubeStore() { release(); }

    // Allocates radiance (count + 2 cubes, full mip chain) and irradiance (count cubes, no mips).
    // Existing storage is released first. Returns false if either allocation fails.
    bool allocate(U32 resolution, U32 count, bool hdr);

    void release();

    bool isAllocated() const { return mRadiance.isValid() && mIrradiance.isValid(); }

    U32 getResolution() const { return mResolution; }
    U32 getCount() const { return mCount; }

    // Full mip count of the radiance array, read back from D3D11 (see DXCubeArrayTexture::getMipLevels).
    U32 getRadianceMipCount() const { return mRadiance.getMipLevels(); }

    // Index of the sphere-probe scratch cube and the realtime-probe scratch cube.
    U32 sphereScratchIndex() const { return mCount; }
    U32 realtimeScratchIndex() const { return mCount + 1; }

    DXCubeArrayTexture& radiance() { return mRadiance; }
    DXCubeArrayTexture& irradiance() { return mIrradiance; }

    ID3D11ShaderResourceView* radianceSRV() const { return mRadiance.getSRV(); }
    ID3D11ShaderResourceView* irradianceSRV() const { return mIrradiance.getSRV(); }

    // Bytes of video memory for this store, including the radiance mip chain.
    U64 memoryBytes() const;

    // Uploads the live orientation controls (RenderCubeConv*) to a generator program.
    // Call after binding the program, for radiance, irradiance and mirror generation.
    static void setGeneratorConvention(LLHLSLShader& shader);

private:
    DXCubeArrayTexture mRadiance;
    DXCubeArrayTexture mIrradiance;
    U32 mResolution = 0;
    U32 mCount = 0;
    bool mHDR = false;
};
