/**
 * @file DXRenderTarget.cpp
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
 * Portions of this file (the functions tagged [LL]) derive from
 * llrendertarget.cpp of the Second Life Viewer and remain under the license
 * below; everything else in this file is under the MIT license above.
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

#include "DXRenderTarget.h"
#include "DXDevice.h"
#include "DXSampler.h"
#include "DXStateCache.h"
#include "DXSwapChain.h"
#include "DXUIBatch.h"
#include "llerror.h"
#include "llrender.h"

// +--------------------------------------------------------------------------+
// | DXRenderTarget - off-screen render target (color attachments + depth)    |
// |                                                                          |
// | Origin tags:  [LL]  Linden logic retained (bind stack, size/viewport)    |
// |               [S24] S24-written, incl. DX versions of Linden functions   |
// | Sections:     STATE | HELPERS | LIFETIME | BINDING | CLEARING |          |
// |               SAMPLING | QUERIES | SWAPPING                              |
// +--------------------------------------------------------------------------+

// +--------------------------------------------------------------------------+
// | STATE - process-wide counters and the bind stack                         |
// +--------------------------------------------------------------------------+

// [S24] running total behind getBytesAllocated()
std::atomic<uint64_t> DXRenderTarget::sBytesAllocated{ 0 };

// [LL] innermost target bound by bindTarget(); null when the back buffer is bound
DXRenderTarget* DXRenderTarget::sBoundTarget = nullptr;

// +--------------------------------------------------------------------------+
// | HELPERS - file-local                                                     |
// +--------------------------------------------------------------------------+

namespace
{
    // [S24] default SRV over all mips of `tex`; logs on failure
    bool createSRVIfPossible(ID3D11Texture2D* tex, DXGI_FORMAT format, ID3D11ShaderResourceView** out_srv)
    {
        HRESULT hr = gDXDevice.getDevice()->CreateShaderResourceView(tex, nullptr, out_srv);
        if (FAILED(hr))
        {
            LL_WARNS("RenderTarget") << "CreateShaderResourceView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
            return false;
        }
        return true;
    }

    // [S24] bytes per texel of every format a target can request, plus D24S8 (feeds the memory stat)
    uint64_t formatBytes(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8_UNORM:
            return 1;
        case DXGI_FORMAT_R16_FLOAT:
            return 2;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return 8;
        default:
            return 4; // R8G8B8A8, B8G8R8A8, R10G10B10A2, R11G11B10, R16G16, D24_S8
        }
    }
}

// +--------------------------------------------------------------------------+
// | LIFETIME - allocation, resizing, release, move                           |
// +--------------------------------------------------------------------------+

// [S24] releases every attachment
DXRenderTarget::~DXRenderTarget()
{
    release();
}

// [S24] move construction: takes the attachments of an unbound target and leaves it empty
DXRenderTarget::DXRenderTarget(DXRenderTarget&& other) noexcept
{
    llassert(!other.isBoundInStack());
    swapContents(other);
}

// [S24] move assignment: releases ours, takes the attachments of an unbound target
DXRenderTarget& DXRenderTarget::operator=(DXRenderTarget&& other) noexcept
{
    if (this != &other)
    {
        llassert(!other.isBoundInStack());
        release();
        swapContents(other);
    }
    return *this;
}

// [S24] (re)allocates the color attachment (UNKNOWN = depth-only) and optional depth buffer
bool DXRenderTarget::allocate(uint32_t width, uint32_t height, DXGI_FORMAT color_format, bool depth, bool generate_mips)
{
    release();

    mGenerateMips = generate_mips;

    mWidth = width;
    mHeight = height;

    if (depth)
    {
        if (!allocateDepth())
        {
            return false;
        }
    }

    // depth-only target (shadow maps): no color attachment
    if (color_format == DXGI_FORMAT_UNKNOWN)
    {
        return true;
    }

    return addColorAttachment(color_format);
}

// [S24] adds a color attachment (max 4) and counts its bytes; UNKNOWN is a no-op
bool DXRenderTarget::addColorAttachment(DXGI_FORMAT color_format)
{
    llassert(!isBoundInStack());

    if (color_format == DXGI_FORMAT_UNKNOWN)
    {
        return true; // No attachment requested
    }

    if (mColor.size() >= 4)
    {
        llassert(mColor.size() < 4);
        return false;
    }

    Attachment att;
    att.format = color_format;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.MipLevels = mGenerateMips ? 0 : 1; // 0 = full chain
    desc.ArraySize = 1;
    desc.Format = color_format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (mGenerateMips)
    {
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }

    HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&desc, nullptr, &att.texture);
    if (FAILED(hr))
    {
        LL_WARNS("RenderTarget") << "CreateTexture2D (color attachment) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    hr = gDXDevice.getDevice()->CreateRenderTargetView(att.texture, nullptr, &att.rtv);
    if (FAILED(hr))
    {
        LL_WARNS("RenderTarget") << "CreateRenderTargetView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        att.texture->Release();
        return false;
    }

    if (!createSRVIfPossible(att.texture, color_format, &att.srv))
    {
        att.rtv->Release();
        att.texture->Release();
        return false;
    }

    att.bytes = (uint64_t)mWidth * mHeight * formatBytes(color_format);
    if (mGenerateMips)
    {
        att.bytes += att.bytes / 3; // a full mip chain adds a third
    }
    mColor.push_back(att);
    sBytesAllocated += att.bytes;
    return true;
}

// [S24] creates the D24S8 depth buffer with its depth-stencil, read-only, depth-SRV and stencil-SRV views
bool DXRenderTarget::allocateDepth()
{
    releaseDepth();

    // TYPELESS with DEPTH_STENCIL|SHADER_RESOURCE so one texture backs the depth-stencil view, the depth SRV
    // and the stencil SRV, each view picking its own concrete format
    mDepthFormat = DXGI_FORMAT_R24G8_TYPELESS;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = mDepthFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&desc, nullptr, &mDepthTexture);
    if (FAILED(hr))
    {
        LL_WARNS("RenderTarget") << "CreateTexture2D (depth) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = gDXDevice.getDevice()->CreateDepthStencilView(mDepthTexture, &dsv_desc, &mDSV);
    if (FAILED(hr))
    {
        LL_WARNS("RenderTarget") << "CreateDepthStencilView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mDepthTexture->Release();
        mDepthTexture = nullptr;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    hr = gDXDevice.getDevice()->CreateShaderResourceView(mDepthTexture, &srv_desc, &mDepthSRV);
    if (FAILED(hr))
    {
        LL_WARNS("RenderTarget") << "CreateShaderResourceView (depth) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mDSV->Release();
        mDSV = nullptr;
        mDepthTexture->Release();
        mDepthTexture = nullptr;
        return false;
    }

    // read-only view for rebindWithDepth(); non-fatal on failure (it falls back to mDSV)
    D3D11_DEPTH_STENCIL_VIEW_DESC ro_dsv_desc = dsv_desc;
    ro_dsv_desc.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    hr = gDXDevice.getDevice()->CreateDepthStencilView(mDepthTexture, &ro_dsv_desc, &mReadOnlyDSV);
    if (FAILED(hr))
    {
        LL_WARNS("RenderTarget") << "CreateDepthStencilView (read-only) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mReadOnlyDSV = nullptr;
    }

    // stencil-plane SRV, only KRLV's @camtextures pass needs it; non-fatal on failure
    D3D11_SHADER_RESOURCE_VIEW_DESC stencil_srv_desc = {};
    stencil_srv_desc.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    stencil_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    stencil_srv_desc.Texture2D.MostDetailedMip = 0;
    stencil_srv_desc.Texture2D.MipLevels = 1;
    hr = gDXDevice.getDevice()->CreateShaderResourceView(mDepthTexture, &stencil_srv_desc, &mStencilSRV);
    if (FAILED(hr))
    {
        LL_WARNS("RenderTarget") << "CreateShaderResourceView (stencil) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mStencilSRV = nullptr;
    }

    mOwnsDepth = true;
    sBytesAllocated += (uint64_t)mWidth * mHeight * formatBytes(DXGI_FORMAT_D24_UNORM_S8_UINT);
    return true;
}

// [S24] lets `target` adopt this target's depth buffer without owning it
void DXRenderTarget::shareDepthBuffer(DXRenderTarget& target)
{
    llassert(!isBoundInStack());

    if (!mDepthTexture)
    {
        return;
    }

    if (target.mDepthTexture)
    {
        LL_WARNS("RenderTarget") << "shareDepthBuffer: target already has a depth buffer" << LL_ENDL;
        return;
    }

    // target adopts our depth resources without owning them
    target.mDepthFormat = mDepthFormat;
    target.mDepthTexture = mDepthTexture;
    target.mDSV = mDSV;
    target.mReadOnlyDSV = mReadOnlyDSV;
    target.mDepthSRV = mDepthSRV;
    target.mStencilSRV = mStencilSRV;
    target.mOwnsDepth = false;
}

// [S24] recreates every attachment at the new size (D3D11 can't resize in place)
void DXRenderTarget::resize(uint32_t width, uint32_t height)
{
    if (width == mWidth && height == mHeight)
    {
        return;
    }

    // recreate everything at the new size from the recorded per-attachment formats
    std::vector<DXGI_FORMAT> color_formats;
    color_formats.reserve(mColor.size());
    for (auto& att : mColor)
    {
        color_formats.push_back(att.format);
    }
    bool had_depth = (mDepthTexture != nullptr) && mOwnsDepth;

    releaseColorAttachments();
    if (mOwnsDepth)
    {
        releaseDepth();
    }

    mWidth = width;
    mHeight = height;

    if (had_depth)
    {
        allocateDepth();
    }

    for (DXGI_FORMAT fmt : color_formats)
    {
        addColorAttachment(fmt);
    }
}

// [S24] frees all attachments; safe to repeat
void DXRenderTarget::release()
{
    llassert(!isBoundInStack());

    releaseColorAttachments();
    releaseDepth();
    mWidth = 0;
    mHeight = 0;
}

// [S24] frees the color attachments and uncounts their bytes
void DXRenderTarget::releaseColorAttachments()
{
    for (auto& att : mColor)
    {
        sBytesAllocated -= att.bytes;
        if (att.srv) att.srv->Release();
        if (att.rtv) att.rtv->Release();
        if (att.texture) att.texture->Release();
    }
    mColor.clear();
}

// [S24] frees the depth buffer if owned (an adopted one is only forgotten) and uncounts its bytes
void DXRenderTarget::releaseDepth()
{
    if (mOwnsDepth)
    {
        if (mDepthTexture)
        {
            sBytesAllocated -= (uint64_t)mWidth * mHeight * formatBytes(DXGI_FORMAT_D24_UNORM_S8_UINT);
        }
        if (mDepthSRV) mDepthSRV->Release();
        if (mStencilSRV) mStencilSRV->Release();
        if (mReadOnlyDSV) mReadOnlyDSV->Release();
        if (mDSV) mDSV->Release();
        if (mDepthTexture) mDepthTexture->Release();
    }
    mDepthSRV = nullptr;
    mStencilSRV = nullptr;
    mReadOnlyDSV = nullptr;
    mDSV = nullptr;
    mDepthTexture = nullptr;
    mDepthFormat = DXGI_FORMAT_UNKNOWN;
    mOwnsDepth = true;
}

// +--------------------------------------------------------------------------+
// | BINDING - the bind stack and the swap chain                              |
// +--------------------------------------------------------------------------+

// [LL] binds all attachments and the viewport, pushing this target on the bind stack
void DXRenderTarget::bindTarget(bool bind_depth)
{
    llassert(!isBoundInStack());

    rebindWithDepth(bind_depth);
    mPreviousRT = sBoundTarget;
    sBoundTarget = this;
}

// [S24] re-issues the output binding without touching the stack (depth view on/off, or read-only)
void DXRenderTarget::rebindWithDepth(bool bind_depth, bool read_only_depth)
{
    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    ID3D11RenderTargetView* rtvs[4] = { nullptr, nullptr, nullptr, nullptr };
    for (size_t i = 0; i < mColor.size(); ++i)
    {
        rtvs[i] = mColor[i].rtv;
    }

    ID3D11DepthStencilView* dsv_to_bind = nullptr;
    if (bind_depth)
    {
        dsv_to_bind = (read_only_depth && mReadOnlyDSV) ? mReadOnlyDSV : mDSV;
    }
    ctx->OMSetRenderTargets((UINT)mColor.size(), rtvs, dsv_to_bind);
    // lets the texture-bind dedup in llrender.cpp notice SRVs D3D11 auto-unbinds when a resource becomes an RTV
    DXStateCache::bumpRTVGeneration();

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)mWidth;
    vp.Height = (float)mHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
}

// [LL] ends rendering: pops to the previous target or the back buffer, then rebuilds mips if requested
void DXRenderTarget::flush()
{
    gDX.flush();

    llassert(sBoundTarget == this);

    if (mPreviousRT)
    {
        // pop to the previous target
        sBoundTarget = mPreviousRT->mPreviousRT;
        mPreviousRT->bindTarget();
    }
    else
    {
        sBoundTarget = nullptr;
        bindSwapChainBackBuffer();

        // bindSwapChainBackBuffer() sets a full-window viewport; restore the world-view rect (gDXViewport) or a
        // mid-frame unwind to the back buffer would widen it
        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = (float)gDXViewport[0];
        vp.TopLeftY = (float)(gDXSwapChain.getHeight() - (gDXViewport[1] + gDXViewport[3]));
        vp.Width = (float)gDXViewport[2];
        vp.Height = (float)gDXViewport[3];
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        gDXDevice.getContext()->RSSetViewports(1, &vp);
    }

    // generate_mips targets (the luminance map) are sampled at a coarse mip
    generateMips();
}

// [S24] binds the back buffer, its depth view and a full-window viewport, and empties the bind stack
void DXRenderTarget::bindSwapChainBackBuffer()
{
    sBoundTarget = nullptr;

    ID3D11DeviceContext* ctx = gDXDevice.getContext();
    ID3D11RenderTargetView* back_buffer = gDXSwapChain.getBackBufferRTV();
    // the swap chain depth view: selections and HUD elements drawn afterwards need a real depth buffer
    ctx->OMSetRenderTargets(1, &back_buffer, gDXSwapChain.getDepthStencilView());
    DXStateCache::bumpRTVGeneration();

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)gDXSwapChain.getWidth();
    vp.Height = (float)gDXSwapChain.getHeight();
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
}

// [LL] true if this target is bound anywhere in the stack
bool DXRenderTarget::isBoundInStack() const
{
    const DXRenderTarget* cur = sBoundTarget;
    while (cur && cur != this)
    {
        cur = cur->mPreviousRT;
    }

    return cur == this;
}

// +--------------------------------------------------------------------------+
// | CLEARING                                                                 |
// +--------------------------------------------------------------------------+

// [S24] clears color and/or depth (depth only if present) to mClearColor and far depth
void DXRenderTarget::clear(bool clear_color, bool clear_depth)
{
    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    if (clear_color)
    {
        // mClearColor, set by clearColor(); defaults to black
        for (auto& att : mColor)
        {
            if (att.rtv)
            {
                ctx->ClearRenderTargetView(att.rtv, mClearColor);
            }
        }
    }

    if (clear_depth && mDSV)
    {
        // 0.0 is far under reversed-Z (see kGLtoDXDepthRemap in llrender.cpp)
        ctx->ClearDepthStencilView(mDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
    }
}

// [S24] sets the remembered clear color and clears the color attachments now
void DXRenderTarget::clearColor(float r, float g, float b, float a)
{
    // remembered for every later clear(true, ...) on this target
    mClearColor[0] = r;
    mClearColor[1] = g;
    mClearColor[2] = b;
    mClearColor[3] = a;

    ID3D11DeviceContext* ctx = gDXDevice.getContext();
    for (auto& att : mColor)
    {
        if (att.rtv)
        {
            ctx->ClearRenderTargetView(att.rtv, mClearColor);
        }
    }
}

// +--------------------------------------------------------------------------+
// | SAMPLING - attachments as shader inputs and mip generation               |
// +--------------------------------------------------------------------------+

// [S24] binds one color attachment as a shader input on `channel` and syncs LLTexUnit's cache
void DXRenderTarget::bindTexture(uint32_t index, int32_t channel, LLTexUnit::eTextureFilterOptions filter_options)
{
    // the chokepoint for the deferred lighting pass's G-buffer reads (diffuse/specular/normal/emissive by index)
    if (channel < 0)
    {
        return;
    }
    ID3D11ShaderResourceView* srv = getColorSRV(index);
    if (!srv)
    {
        return;
    }
    // always flush: pending batched vertices must not be drawn under the new SRV (no LLTexUnit cache to consult here)
    gDX.flush();
    // this binds outside LLTexUnit's bookkeeping, so flush the UI batch or a later LLTexUnit::bind() could skip a real rebind
    gDXUIBatch.flushPending();
    // CLAMP addressing; the caller's filter is honoured (encoded normals and packed material params are wrong, not
    // just soft, if bilinear-interpolated)
    ID3D11SamplerState* sampler = DXSampler::getOrCreate(2, (int)filter_options);
    bool bind_ok = safeBindSRV(gDXDevice.getContext(), channel, srv);
    if (channel < 16) // see LLTexUnit::bindFast()'s comment - sampler slots cap at 16, SRV slots don't
    {
        gDXDevice.getContext()->PSSetSamplers(channel, 1, &sampler);
    }
    // tell the channel's LLTexUnit what is bound now; only on success, or its cache would never retry a caught bad bind
    if (bind_ok)
    {
        gDX.getTexUnit(channel)->syncDXBindState((void*)srv, (void*)sampler);
    }
}

// [S24] rebuilds the mip chain of every color attachment from its top level
void DXRenderTarget::generateMips()
{
    if (!mGenerateMips)
    {
        return;
    }
    for (auto& att : mColor)
    {
        if (att.srv)
        {
            gDXDevice.getContext()->GenerateMips(att.srv);
        }
    }
}

// [S24] color attachment SRV, or null if out of range
ID3D11ShaderResourceView* DXRenderTarget::getColorSRV(size_t index) const
{
    if (index >= mColor.size())
    {
        return nullptr;
    }
    return mColor[index].srv;
}

// [S24] color attachment texture for raw CPU<->GPU transfer, or null if out of range
ID3D11Texture2D* DXRenderTarget::getColorTexture(size_t index) const
{
    if (index >= mColor.size())
    {
        return nullptr;
    }
    return mColor[index].texture;
}

// +--------------------------------------------------------------------------+
// | QUERIES                                                                  |
// +--------------------------------------------------------------------------+

// [LL] fills viewport[4] with {0, 0, width, height}
void DXRenderTarget::getViewport(int32_t* viewport) const
{
    viewport[0] = 0;
    viewport[1] = 0;
    viewport[2] = (int32_t)mWidth;
    viewport[3] = (int32_t)mHeight;
}

// +--------------------------------------------------------------------------+
// | SWAPPING                                                                 |
// +--------------------------------------------------------------------------+

// [S24] *HACK exchanges the attachments of two same-sized, unbound targets
void DXRenderTarget::swapTargets(DXRenderTarget& other)
{
    // Preconditions: both valid, unbound, and compatible
    llassert(!isBoundInStack() && !other.isBoundInStack());
    llassert(mWidth == other.mWidth && mHeight == other.mHeight);

    swapContents(other);
}

// [S24] exchanges every member except the bind stack link (shared by swap and move)
void DXRenderTarget::swapContents(DXRenderTarget& other) noexcept
{
    // member by member: the raw COM pointers have no copy or move semantics, and the bind stack link
    // (mPreviousRT) stays with the object
    using std::swap;
    swap(mColor, other.mColor);
    for (int i = 0; i < 4; ++i)
    {
        swap(mClearColor[i], other.mClearColor[i]);
    }
    swap(mDepthFormat, other.mDepthFormat);
    swap(mDepthTexture, other.mDepthTexture);
    swap(mDSV, other.mDSV);
    swap(mReadOnlyDSV, other.mReadOnlyDSV);
    swap(mDepthSRV, other.mDepthSRV);
    swap(mStencilSRV, other.mStencilSRV);
    swap(mOwnsDepth, other.mOwnsDepth);
    swap(mGenerateMips, other.mGenerateMips);
    swap(mWidth, other.mWidth);
    swap(mHeight, other.mHeight);
}
