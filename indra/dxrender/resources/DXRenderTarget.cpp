#include "DXRenderTarget.h"
#include "DXDevice.h"
#include "DXSwapChain.h"
#include "llerror.h"

namespace
{
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
}

bool DXRenderTarget::allocate(uint32_t width, uint32_t height, DXGI_FORMAT color_format, bool depth)
{
    release();

    mWidth = width;
    mHeight = height;

    if (depth)
    {
        if (!allocateDepth())
        {
            return false;
        }
    }

    return addColorAttachment(color_format);
}

bool DXRenderTarget::addColorAttachment(DXGI_FORMAT color_format)
{
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
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = color_format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

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

    mColor.push_back(att);
    return true;
}

bool DXRenderTarget::allocateDepth()
{
    releaseDepth();

    // S24 (2026-08-04): was DXGI_FORMAT_D24_UNORM_S8_UINT with
    // BindFlags = D3D11_BIND_DEPTH_STENCIL only - a depth-stencil format
    // texture created WITHOUT D3D11_BIND_SHADER_RESOURCE can never have an
    // SRV created on it (D3D11 requires the bind flag be present at
    // creation time), so mDepthSRV was never actually creatable and every
    // getDepthSRV() caller (LLTexUnit::bind(LLRenderTarget*, true) et al)
    // silently got null and no-op'd - meaning depth was NEVER real read as
    // a texture anywhere this whole port, and any shader sampling it
    // (softenLightF.hlsl's getDepth() being the first) read whatever
    // unrelated SRV happened to already be bound to that texture slot from
    // an earlier draw call. Root cause of a "grey screen with interference
    // patterns/blobs/kinked lines" symptom - textbook garbage-bit-pattern
    // visualization, not a math bug. Real fix: create the texture TYPELESS
    // (D3D11's standard depth+shader-resource pattern) with BOTH bind
    // flags, then create the DSV/SRV with explicit, different concrete
    // formats - D24_UNORM_S8_UINT for depth-test purposes, and
    // R24_UNORM_X8_TYPELESS (reads just the 24-bit depth channel as a
    // normalized float, ignoring the 8-bit stencil) for sampling.
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

    // S24 (2026-08-15): read-only companion to mDSV, same texture/format -
    // see this header's bindTarget() comment for why this exists. Failure
    // here is non-fatal (matches this depth buffer's pre-existing, already-
    // working behavior when read-only binding isn't requested) - just leaves
    // mReadOnlyDSV null, and bindTarget() falls back to mDSV.
    D3D11_DEPTH_STENCIL_VIEW_DESC ro_dsv_desc = dsv_desc;
    ro_dsv_desc.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    hr = gDXDevice.getDevice()->CreateDepthStencilView(mDepthTexture, &ro_dsv_desc, &mReadOnlyDSV);
    if (FAILED(hr))
    {
        LL_WARNS("RenderTarget") << "CreateDepthStencilView (read-only) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mReadOnlyDSV = nullptr;
    }

    mOwnsDepth = true;
    return true;
}

void DXRenderTarget::shareDepthBuffer(DXRenderTarget& target)
{
    if (!mDepthTexture)
    {
        return;
    }

    if (target.mDepthTexture)
    {
        LL_WARNS("RenderTarget") << "shareDepthBuffer: target already has a depth buffer" << LL_ENDL;
        return;
    }

    // Adopt our depth resources - target does not own them, so its
    // release()/destructor must not free them.
    target.mDepthFormat = mDepthFormat;
    target.mDepthTexture = mDepthTexture;
    target.mDSV = mDSV;
    target.mReadOnlyDSV = mReadOnlyDSV;
    target.mDepthSRV = mDepthSRV;
    target.mOwnsDepth = false;
}

void DXRenderTarget::resize(uint32_t width, uint32_t height)
{
    if (width == mWidth && height == mHeight)
    {
        return;
    }

    // D3D11 resources can't be resized in place - recreate everything at
    // the new dimensions using the formats already recorded per attachment.
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

void DXRenderTarget::releaseColorAttachments()
{
    for (auto& att : mColor)
    {
        if (att.srv) att.srv->Release();
        if (att.rtv) att.rtv->Release();
        if (att.texture) att.texture->Release();
    }
    mColor.clear();
}

void DXRenderTarget::releaseDepth()
{
    if (mOwnsDepth)
    {
        if (mDepthSRV) mDepthSRV->Release();
        if (mReadOnlyDSV) mReadOnlyDSV->Release();
        if (mDSV) mDSV->Release();
        if (mDepthTexture) mDepthTexture->Release();
    }
    mDepthSRV = nullptr;
    mReadOnlyDSV = nullptr;
    mDSV = nullptr;
    mDepthTexture = nullptr;
    mDepthFormat = DXGI_FORMAT_UNKNOWN;
    mOwnsDepth = true;
}

void DXRenderTarget::release()
{
    releaseColorAttachments();
    releaseDepth();
    mWidth = 0;
    mHeight = 0;
}

void DXRenderTarget::bindTarget(bool bind_depth, bool read_only_depth)
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

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)mWidth;
    vp.Height = (float)mHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
}

void DXRenderTarget::clear(bool clear_color, bool clear_depth)
{
    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    if (clear_color)
    {
        // S24 (2026-08-17, task #174): was a hardcoded `black[4]` here,
        // unconditionally, regardless of what color the caller actually
        // wanted (GL's own clear() honors whatever ambient glClearColor()
        // was last set - D3D11 has no equivalent ambient state, so nothing
        // here ever reflected it). mClearColor defaults to black too, so
        // any target that never calls clearColor() behaves exactly as
        // before - this only changes behavior for targets that have.
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
        ctx->ClearDepthStencilView(mDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
}

void DXRenderTarget::clearColor(float r, float g, float b, float a)
{
    // S24 (2026-08-17, task #174): remember it for every future clear(true, ...)
    // call on this target too, not just this one-shot clear - see this
    // function's header comment.
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

// static
void DXRenderTarget::bindSwapChainBackBuffer()
{
    ID3D11DeviceContext* ctx = gDXDevice.getContext();
    ID3D11RenderTargetView* back_buffer = gDXSwapChain.getBackBufferRTV();
    // S24 (2026-08-09): see DXSwapChain::getDepthStencilView()'s comment -
    // was nullptr here, meaning nothing drawn after this call (including
    // everything LLViewerWindow::renderSelections()/render_hud_elements()
    // draw, later this same frame) had a real depth buffer to test against.
    ctx->OMSetRenderTargets(1, &back_buffer, gDXSwapChain.getDepthStencilView());

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)gDXSwapChain.getWidth();
    vp.Height = (float)gDXSwapChain.getHeight();
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
}

ID3D11ShaderResourceView* DXRenderTarget::getColorSRV(size_t index) const
{
    if (index >= mColor.size())
    {
        return nullptr;
    }
    return mColor[index].srv;
}

ID3D11Texture2D* DXRenderTarget::getColorTexture(size_t index) const
{
    if (index >= mColor.size())
    {
        return nullptr;
    }
    return mColor[index].texture;
}
