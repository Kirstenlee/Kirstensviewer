/**
 * @file llboxprobes.h
 * @brief LLBoxProbes - box-volume reflection probes.
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

#include "llreflectionmap.h"
#include "DXBuffer.h"

#include <vector>

// Box-volume probes. Their capture is scheduled by LLSphereProbes, like every probe, and they
// are read from here: their own packing, depth buckets, uniform block (register b2) and shader
// entry. Box probes never appear in the sphere uniform block.
class LLBoxProbes
{
public:
    // Must byte-match cbuffer BoxProbes in class3/deferred/reflectionProbeF.hlsl.
    // Size is a multiple of 16 bytes.
    struct Data
    {
        // camera space -> unit box of each probe's volume
        LLMatrix4 boxRefBox[LL_MAX_REFLECTION_PROBE_COUNT];
        // xyz: probe origin in camera space, w: radius
        LLVector4 boxRefOrigin[LL_MAX_REFLECTION_PROBE_COUNT];
        // x: unused (0), y: radiance scale, z: fade in
        LLVector4 boxRefParams[LL_MAX_REFLECTION_PROBE_COUNT];
        // [0]: cube slot in the radiance array
        GLint boxRefIndex[LL_MAX_REFLECTION_PROBE_COUNT][4];
        // [i][0]: first boxRefIndex entry that can influence depth bucket i (1 m per bucket)
        GLint boxRefBucket[256][4];
        GLint boxCount;
        GLint pad[3];
    };

    // Packs the box probes from maps (sorted by depth, default probe excluded) and uploads them.
    // radscale: radiance scale for this frame (sphere set's reset-fade/capture scale).
    void update(const std::vector<LLReflectionMap*>& maps, U32 max_count, F32 radscale);

    // Binds the box uniform block to the pixel stage.
    void bind();

    void destroy();

    U32 count() const { return (U32)mData.boxCount; }

private:
    void upload();

    Data mData;
    DXBuffer mDXUBO;
};
