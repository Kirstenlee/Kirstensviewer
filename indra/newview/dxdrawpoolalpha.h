/**
 * @file dxdrawpoolalpha.h
 * @brief Fresh DX11-native implementation of DXAlphaDrawPool's forward-alpha
 * render path.
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

#include "lldrawpool.h"
#include "llrender.h"

// DX alpha draw pool: the LLRenderPass that owns the forward-alpha batches.
// Rendering is done by DXDrawPoolAlpha (static helpers below).
class DXAlphaDrawPool final: public LLRenderPass
{
public:
    enum
    {
        VERTEX_DATA_MASK =  LLVertexBuffer::MAP_VERTEX |
                            LLVertexBuffer::MAP_NORMAL |
                            LLVertexBuffer::MAP_COLOR |
                            LLVertexBuffer::MAP_TEXCOORD0
    };
    virtual U32 getVertexDataMask() { return VERTEX_DATA_MASK; }

    DXAlphaDrawPool(U32 type);
    /*virtual*/ ~DXAlphaDrawPool();

    /*virtual*/ S32 getNumPostDeferredPasses();
    /*virtual*/ void renderPostDeferred(S32 pass);
    /*virtual*/ S32  getNumPasses() { return 1; }

    /*virtual*/ void prerender();
};

// Deliberate exception to this stage's usual "thin redirect, minimal diff"
// shape: this is a staged, purpose-built DUPLICATE of
// DXAlphaDrawPool::renderPostDeferred()'s call graph (forwardRender(),
// renderAlpha(), the emissive helpers, renderDebugAlpha()/
// renderAlphaHighlight()), not a from-scratch redesign. Chosen deliberately
// over an in-place #ifdef because most of that call graph is already
// DX-safe by composition (LLGLDepthTest/blendFunc/LLGLDisable/
// LLHLSLShader::bind()+bindTexture()/LLVertexBuffer - all fixed in earlier
// phases) and only rigged-batch handling needed to change - see the stage 5
// hitlist memory for the full reasoning. Understand this means the two
// copies can and will drift on future GL-side edits/LL merges; that's an
// accepted tradeoff for keeping the pools API-distinct going forward.
//
// Rigged (skinned) batches (mesh bodies/clothing/attachments) get their own
// pass, in the original GL alpha pool's two-pass rigged/non-rigged shape
// (PASS_ALPHA vs PASS_ALPHA_RIGGED, beginAlphaGroups() vs
// beginRiggedAlphaGroups(), mRiggedVariant shader selection,
// uploadMatrixPalette() per-batch).
class DXDrawPoolAlpha
{
public:
    static void renderPostDeferred(DXAlphaDrawPool& pool, S32 pass);

    // Shared alpha-pool state. Owned here now that the GL pool is being removed.
    static LLVector4 sWaterPlane;   // water plane in eye space, set by llsettingsvo
    static bool sShowDebugAlpha;    // debug alpha view toggle
};
