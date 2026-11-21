/**
 * @file DXRenderTarget.h
 * @brief Off-screen render target: color attachments plus an optional depth buffer
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
 *
 * Portions of this file (the members tagged [LL]) derive from llrendertarget.h
 * of the Second Life Viewer and remain under the license below; everything
 * else in this file is under the MIT license above.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#pragma once

// llrender.h first: it pulls in llwin32headers.h, which must precede <windows.h> (via d3d11.h)
#include "llrender.h"

#include <d3d11.h>
#include <vector>
#include <cstdint>
#include <atomic>

// +--------------------------------------------------------------------------+
// | DXRenderTarget - off-screen render target (color attachments + depth)    |
// |                                                                          |
// | Origin tags:  [LL]  Linden logic retained (bind stack, size/viewport)    |
// |               [S24] S24-written, incl. DX versions of Linden functions   |
// | Sections:     STATE | LIFETIME | BINDING | CLEARING | SAMPLING | QUERIES |
// |               SWAPPING                                                   |
// +--------------------------------------------------------------------------+
//
// Usage:
//     DXRenderTarget target;
//     target.allocate(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);   // with depth
//     target.bindTarget();  target.clear();  /* draw */  target.flush();
//     gDX.getTexUnit(i)->bind(&target);                              // sample it

class DXRenderTarget
{
public:
    // +----------------------------------------------------------------------+
    // | STATE - process-wide counters and the bind stack                     |
    // +----------------------------------------------------------------------+

    // [S24] total bytes held by every live target's own textures (a shared depth buffer counts once, in its owner)
    static uint64_t getBytesAllocated() { return sBytesAllocated.load(std::memory_order_relaxed); }

    // [LL] innermost target bound by bindTarget(); null when the back buffer is bound
    static DXRenderTarget* sBoundTarget;

    // +----------------------------------------------------------------------+
    // | LIFETIME - allocation, resizing, release, move                       |
    // +----------------------------------------------------------------------+

    // [S24] a default-constructed target is empty; the destructor releases every attachment
    DXRenderTarget() = default;
    ~DXRenderTarget();

    // [S24] move-only: copying raw COM pointers would double-release. A move transfers the attachments and
    // empties the source, which is what lets targets live in a std::vector.
    DXRenderTarget(const DXRenderTarget&) = delete;
    DXRenderTarget& operator=(const DXRenderTarget&) = delete;
    DXRenderTarget(DXRenderTarget&& other) noexcept;
    DXRenderTarget& operator=(DXRenderTarget&& other) noexcept;

    // [S24] (re)allocates the color attachment (DXGI_FORMAT_UNKNOWN = depth-only) and optional depth buffer;
    // generate_mips adds a mip chain that flush() rebuilds
    bool allocate(uint32_t width, uint32_t height, DXGI_FORMAT color_format, bool depth = false, bool generate_mips = false);

    // [S24] adds a color attachment (max 4); DXGI_FORMAT_UNKNOWN is a no-op
    bool addColorAttachment(DXGI_FORMAT color_format);

    // [S24] creates the D24S8 depth buffer with its depth-stencil, read-only, depth-SRV and stencil-SRV views
    bool allocateDepth();

    // [S24] `target` adopts this target's depth buffer without owning it
    void shareDepthBuffer(DXRenderTarget& target);

    // [S24] recreates every attachment at the new size (D3D11 can't resize in place); on GPU out-of-memory the
    // target is left undefined, so not for screen-space buffers or scratch space
    void resize(uint32_t width, uint32_t height);

    // [S24] frees all attachments; safe to repeat; asserts the target is not bound
    void release();

    // +----------------------------------------------------------------------+
    // | BINDING - the bind stack and the swap chain                          |
    // +----------------------------------------------------------------------+

    // [LL] binds all attachments and the viewport and pushes this target on the bind stack (flush() pops it).
    // bind_depth=false omits the depth view for passes that also sample this target's depth, which D3D11
    // forbids binding as output and input at once.
    void bindTarget(bool bind_depth = true);

    // [S24] re-issues the output binding without touching the stack; read_only_depth binds the read-only depth
    // view so a pass can depth-test and sample the same depth (the local-light pass)
    void rebindWithDepth(bool bind_depth, bool read_only_depth = false);

    // [LL] ends rendering, 1:1 with bindTarget(): restores the previous target (or the back buffer), then
    // rebuilds the mip chain if requested
    void flush();

    // [S24] binds the back buffer, its depth view and a full-window viewport, and empties the bind stack so
    // no stale target is restored later
    static void bindSwapChainBackBuffer();

    // [LL] true if this target is bound anywhere in the stack
    bool isBoundInStack() const;

    // +----------------------------------------------------------------------+
    // | CLEARING                                                             |
    // +----------------------------------------------------------------------+

    // [S24] clears color and/or depth (depth only if present) to mClearColor and far depth
    void clear(bool color = true, bool depth = true);

    // [S24] sets the remembered clear color and clears now; D3D11 has no ambient clear color
    void clearColor(float r, float g, float b, float a);

    // +----------------------------------------------------------------------+
    // | SAMPLING - using attachments as shader inputs or raw textures        |
    // +----------------------------------------------------------------------+

    // [S24] binds one color attachment as a shader input on `channel` and syncs LLTexUnit's cache
    void bindTexture(uint32_t index, int32_t channel, LLTexUnit::eTextureFilterOptions filter_options = LLTexUnit::TFO_BILINEAR);

    // [S24] shader-resource views of the color attachment, the depth plane and the stencil plane
    // (X24_TYPELESS_G8_UINT view of the same texture, sampled by the KRLV @camtextures pass)
    ID3D11ShaderResourceView* getColorSRV(size_t index) const;
    ID3D11ShaderResourceView* getDepthSRV() const { return mDepthSRV; }
    ID3D11ShaderResourceView* getStencilSRV() const { return mStencilSRV; }

    // [S24] raw textures for CPU<->GPU transfer (DXReadback, DXCompositionSurface), not shader sampling
    ID3D11Texture2D* getColorTexture(size_t index) const;
    ID3D11Texture2D* getDepthTexture() const { return mDepthTexture; }

    // +----------------------------------------------------------------------+
    // | QUERIES                                                              |
    // +----------------------------------------------------------------------+

    // [LL] size in pixels
    uint32_t getWidth() const { return mWidth; }
    uint32_t getHeight() const { return mHeight; }

    // [LL] fills viewport[4] with {0, 0, width, height}
    void getViewport(int32_t* viewport) const;

    // [S24] number of color attachments
    size_t getNumColorAttachments() const { return mColor.size(); }

    // [S24] true if there is at least one color or depth attachment to render into
    bool isComplete() const { return !mColor.empty() || mDepthTexture != nullptr; }

    // +----------------------------------------------------------------------+
    // | SWAPPING                                                             |
    // +----------------------------------------------------------------------+

    // [S24] *HACK exchanges the attachments of two same-sized, unbound targets (glTF preview, SSAO history)
    void swapTargets(DXRenderTarget& other);

private:
    struct Attachment
    {
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        uint64_t bytes = 0;
    };

    // [S24] frees the color attachments / the depth buffer (only if owned)
    void releaseColorAttachments();
    void releaseDepth();

    // [S24] rebuilds the mip chain of every color attachment from its top level
    void generateMips();

    // [S24] exchanges everything except the bind stack link
    void swapContents(DXRenderTarget& other) noexcept;

    static std::atomic<uint64_t> sBytesAllocated;

    std::vector<Attachment> mColor;
    float mClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // depth buffer; mReadOnlyDSV is a second view of the same texture (may be null, falls back to mDSV)
    DXGI_FORMAT mDepthFormat = DXGI_FORMAT_UNKNOWN;
    ID3D11Texture2D* mDepthTexture = nullptr;
    ID3D11DepthStencilView* mDSV = nullptr;
    ID3D11DepthStencilView* mReadOnlyDSV = nullptr;
    ID3D11ShaderResourceView* mDepthSRV = nullptr;
    ID3D11ShaderResourceView* mStencilSRV = nullptr;
    // false when the depth buffer was adopted via shareDepthBuffer(): release() must not free it
    bool mOwnsDepth = true;

    bool mGenerateMips = false;

    // the target bound when this one was bound, restored by flush()
    DXRenderTarget* mPreviousRT = nullptr;

    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
};
