/**
 * @file dxdrawpoolwlsky.h
 * @brief LLDrawPoolWLSky class definition plus its DX11-native windlight
 * sky dome/clouds/stars/sun/moon render path - lldrawpoolwlsky.h/.cpp
 * retired entirely, 2026-10-13.
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

// The real sky/sun/moon/star/cloud rendering (renderDome/
// renderSkyHazeDeferred/renderSkyCloudsDeferred/renderStarsDeferred/
// renderHeavenlyBodies, and the shader-pointer statics supporting them)
// lives entirely below in DXDrawPoolWLSky. This class is just the
// LLDrawPool-required scaffolding plus the bridging redirects.
class LLDrawPoolWLSky : public LLDrawPool {
public:

    static const U32 SKY_VERTEX_DATA_MASK = LLVertexBuffer::MAP_VERTEX |
                            LLVertexBuffer::MAP_TEXCOORD0;
    static const U32 STAR_VERTEX_DATA_MASK =    LLVertexBuffer::MAP_VERTEX |
        LLVertexBuffer::MAP_COLOR | LLVertexBuffer::MAP_TEXCOORD0;
    static const U32 ADV_ATMO_SKY_VERTEX_DATA_MASK = LLVertexBuffer::MAP_VERTEX
                                                   | LLVertexBuffer::MAP_TEXCOORD0;
    LLDrawPoolWLSky(void);
    /*virtual*/ ~LLDrawPoolWLSky();

    /*virtual*/ bool isDead() { return false; }

    /*virtual*/ S32 getNumDeferredPasses() { return 1; }
    /*virtual*/ void beginDeferredPass(S32 pass);
    /*virtual*/ void endDeferredPass(S32 pass);
    /*virtual*/ void renderDeferred(S32 pass);

    /*virtual*/ LLViewerTexture *getDebugTexture();
    /*virtual*/ U32 getVertexDataMask() { return SKY_VERTEX_DATA_MASK; }
    /*virtual*/ bool verify() const { return true; }        // Verify that all data in the draw pool is correct!
    /*virtual*/ S32 getShaderLevel() const { return mShaderLevel; }

    //static LLDrawPool* createPool(const U32 type, LLViewerTexture *tex0 = NULL);

    /*virtual*/ LLViewerTexture* getTexture();
    /*virtual*/ bool isFacePool() { return false; }
    /*virtual*/ void resetDrawOrders();

    static void cleanupGL();
    static void restoreGL();
};

// Staged full duplicate, same pattern as dxdrawpoolalpha/dxdrawpoolbump -
// keeps pools API-distinct even where the GL body needs little change.
//
// Unlike DXAlphaDrawPool/LLDrawPoolBump, this pool has no rigged-vs-static
// distinction (sky/cloud/star/sun/moon geometry isn't per-object). The
// whole render path is DX-safe by composition: matrix ops go through
// LLRender's CPU-side matrix stack, draws go through LLVOWLSky's plain
// LLVertexBuffer calls, and shader/texture calls go through the DX-safe
// LLHLSLShader/LLTexUnit wrappers.
//
// One real difference: LLDrawPoolWLSky::endDeferredPass()'s raw
// glClear(GL_DEPTH_BUFFER_BIT) (clearing the depth buffer so haze shaders
// can use unwritten depth as a mask) is replaced here with
// gPipeline.mRT->deferredScreen.clear(GL_DEPTH_BUFFER_BIT), using
// LLRenderTarget::clear()'s DX_RENDER branch for the equivalent
// depth-only clear.
class DXDrawPoolWLSky
{
public:
    static void beginDeferredPass(LLDrawPoolWLSky& pool, S32 pass);
    static void endDeferredPass(LLDrawPoolWLSky& pool, S32 pass);
    static void renderDeferred(LLDrawPoolWLSky& pool, S32 pass);
};
