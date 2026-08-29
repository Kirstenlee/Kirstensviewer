/**
 * @file llgpuresize.h
 * @brief GPU-side separable Catmull-Rom bicubic resize of one LLRenderTarget
 * into another, arbitrary source/destination dimensions.
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

class LLRenderTarget;

// S24 (2026-08-26, task #263): built for the snapshot system (a 4K "native
// re-render" request was found to blow past the GPU driver's TDR timeout
// and take the whole device down - see project memory task #263), but
// deliberately generic - cross-backend (GL and DX_RENDER both, same as
// LLPipeline::generateGlow() which this is modeled on), reusable anywhere
// a render target needs a genuinely good GPU resize (thumbnail generation,
// future UI scaling), not snapshot-specific.
class LLGPUResize
{
public:
    // Resizes src into dst via two separable Catmull-Rom bicubic passes
    // (horizontal then vertical) through an internal scratch target sized
    // (dst->getWidth(), src->getHeight()). dst must already be allocated at
    // the target width/height - same "caller allocates, we don't" contract
    // every other LLRenderTarget consumer in this codebase already follows.
    // src and dst must be color-only (RGBA) targets - no depth handling.
    // Returns false (dst left untouched) if gResizeBicubicProgram isn't
    // ready (shader failed to compile) or src/dst are null/unallocated.
    static bool resize(LLRenderTarget* src, LLRenderTarget* dst);
};
