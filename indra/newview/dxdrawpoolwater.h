/**
 * @file dxdrawpoolwater.h
 * @brief LLDrawPoolWater class definition plus its DX11-native post-deferred
 * water surface render path - lldrawpoolwater.h/.cpp retired entirely,
 * 2026-10-13.
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

#pragma once

#include "dxdrawpool.h"

class LLFace;
class LLHeavenBody;
class LLWaterSurface;
class LLHLSLShader;

class LLDrawPoolWater final: public LLFacePool
{
    // DXDrawPoolWater duplicates renderPostDeferred()'s DX_RENDER path (same
    // pattern as DXDrawPoolBump/DXDrawPoolAlpha) and needs mWaterNormp for
    // the wave normal-map blend - stays protected, nothing else needs it public.
    friend class DXDrawPoolWater;
protected:
    LLPointer<LLViewerTexture> mWaterImagep[2];
    LLPointer<LLViewerTexture> mWaterNormp[2];

    LLPointer<LLViewerTexture> mOpaqueWaterImagep;

public:
    static bool sSkipScreenCopy;
    static bool sNeedsReflectionUpdate;
    static bool sNeedsDistortionUpdate;
    static F32 sWaterFogEnd;

    enum
    {
        VERTEX_DATA_MASK =  LLVertexBuffer::MAP_VERTEX |
                            LLVertexBuffer::MAP_NORMAL |
                            LLVertexBuffer::MAP_TEXCOORD0
    };

    virtual U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

    LLDrawPoolWater();
    /*virtual*/ ~LLDrawPoolWater();

    S32 getNumPostDeferredPasses() override;
    void beginPostDeferredPass(S32 pass) override;
    void renderPostDeferred(S32 pass) override;

    void prerender() override;

    LLViewerTexture *getDebugTexture() override;
    LLColor3 getDebugColor() const; // For AGP debug display

    void setTransparentTextures(const LLUUID& transparentTextureId, const LLUUID& nextTransparentTextureId);
    void setOpaqueTexture(const LLUUID& opaqueTextureId);
    void setNormalMaps(const LLUUID& normalMapId, const LLUUID& nextNormalMapId);

    void pushWaterPlanes(int pass);
};

// Mirrors the DXDrawPoolBump/DXDrawPoolAlpha pattern: a dedicated DX-native
// pool class rather than #ifdef blocks in LLDrawPoolWater.
//
// WATER_SCREENTEX/WATER_EXCLUSIONTEX go through the LLHLSLShader::bindTexture
// (S32, LLRenderTarget*, ...) overload - see llhlslshader.cpp.
//
// mWaterExclusionMask (filled by LLDrawPoolWaterExclusion) must be rendered
// every frame before this pool's WATER_EXCLUSIONTEX bind reads it - see
// DXPipeline::doWaterExclusionMask() and DXPipeline::renderGeomPostDeferred().
class DXDrawPoolWater
{
public:
    static void beginPostDeferredPass(LLDrawPoolWater& pool, S32 pass);
    static void renderPostDeferred(LLDrawPoolWater& pool, S32 pass);
};
