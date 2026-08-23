#include "DXSwapChain.h"
#include "DXDevice.h"
#include "llerror.h"
#include <dxgi1_5.h>

DXSwapChain gDXSwapChain;

bool DXSwapChain::create(HWND hwnd, int width, int height, bool vsync)
{
    ID3D11Device* device = gDXDevice.getDevice();
    if (!device)
    {
        LL_WARNS("DXRender") << "DXSwapChain::create called before DXDevice is initialized" << LL_ENDL;
        return false;
    }

    IDXGIDevice* dxgi_device = nullptr;
    HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device);
    if (FAILED(hr) || !dxgi_device)
    {
        LL_WARNS("DXRender") << "Failed to query IDXGIDevice from ID3D11Device" << LL_ENDL;
        return false;
    }

    IDXGIAdapter* adapter = nullptr;
    dxgi_device->GetAdapter(&adapter);
    dxgi_device->Release();

    IDXGIFactory* factory = nullptr;
    if (adapter)
    {
        adapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory);
        adapter->Release();
    }

    if (!factory)
    {
        LL_WARNS("DXRender") << "Failed to get IDXGIFactory from device adapter" << LL_ENDL;
        return false;
    }

    // S24 (task #210): tearing support is a per-adapter/driver/OS feature,
    // not guaranteed (needs Windows 10 1511+ and a compatible driver) - only
    // trust it if IDXGIFactory5::CheckFeatureSupport says yes. A missing
    // IDXGIFactory5 (older OS) just means mAllowTearing stays false, not a
    // hard error.
    mAllowTearing = false;
    IDXGIFactory5* factory5 = nullptr;
    if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&factory5)) && factory5)
    {
        BOOL allow_tearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing))))
        {
            mAllowTearing = (allow_tearing != FALSE);
        }
        factory5->Release();
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 0;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;

    // S24 (task #210): flip model (required for DXGI_PRESENT_ALLOW_TEARING,
    // and a strict upgrade over BitBlt-model DISCARD even without tearing -
    // avoids composition-copy stutter/latency on Windows 10+) attempted
    // first; falls back to the old BitBlt-model DISCARD if creation fails
    // (e.g. an exotic/older driver that advertises D3D11 support but not
    // the flip-model swap effect) so a rare failure here degrades gracefully
    // instead of leaving the viewer unable to start.
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = mAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    hr = factory->CreateSwapChain(device, &desc, &mSwapChain);
    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "CreateSwapChain (FLIP_DISCARD) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec
                              << " - falling back to BitBlt-model DISCARD" << LL_ENDL;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        desc.Flags = 0;
        mAllowTearing = false;
        hr = factory->CreateSwapChain(device, &desc, &mSwapChain);
    }
    factory->Release();

    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "CreateSwapChain failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    mFlipModel = (desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD);
    mSwapChainFlags = desc.Flags;

    mWidth = width;
    mHeight = height;
    mVSync = vsync;

    return createBackBufferRTV() && createDepthStencilView();
}

bool DXSwapChain::createDepthStencilView()
{
    ID3D11Device* device = gDXDevice.getDevice();

    // Matches the fixed DXGI_FORMAT_D24_UNORM_S8_UINT convention already
    // used everywhere else in this codebase (see DXRenderTarget::
    // allocateDepth()'s comment) - this buffer is never sampled as a
    // texture (no D3D11_BIND_SHADER_RESOURCE needed, unlike the G-buffer
    // depth's typeless-format dance), so a plain depth-stencil-only texture
    // is enough here.
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &mDepthStencilTexture);
    if (FAILED(hr) || !mDepthStencilTexture)
    {
        LL_WARNS("DXRender") << "SwapChain depth-stencil CreateTexture2D failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    hr = device->CreateDepthStencilView(mDepthStencilTexture, nullptr, &mDepthStencilView);
    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "SwapChain CreateDepthStencilView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        releaseDepthStencilView();
        return false;
    }

    return true;
}

void DXSwapChain::releaseDepthStencilView()
{
    if (mDepthStencilView) { mDepthStencilView->Release(); mDepthStencilView = nullptr; }
    if (mDepthStencilTexture) { mDepthStencilTexture->Release(); mDepthStencilTexture = nullptr; }
}

void DXSwapChain::releaseBackBufferRTV()
{
    if (mBackBufferRTV) { mBackBufferRTV->Release(); mBackBufferRTV = nullptr; }
}

bool DXSwapChain::createBackBufferRTV()
{
    ID3D11Device* device = gDXDevice.getDevice();

    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT hr = mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back_buffer);
    if (FAILED(hr) || !back_buffer)
    {
        LL_WARNS("DXRender") << "SwapChain GetBuffer failed" << LL_ENDL;
        return false;
    }

    hr = device->CreateRenderTargetView(back_buffer, nullptr, &mBackBufferRTV);
    back_buffer->Release();

    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "CreateRenderTargetView failed" << LL_ENDL;
        return false;
    }

    return true;
}

void DXSwapChain::destroy()
{
    releaseBackBufferRTV();
    releaseDepthStencilView();
    if (mSwapChain) { mSwapChain->Release(); mSwapChain = nullptr; }
}

bool DXSwapChain::resize(int width, int height)
{
    if (!mSwapChain)
    {
        return false;
    }

    releaseBackBufferRTV();
    releaseDepthStencilView();

    // S24 (task #210): ResizeBuffers' own Flags parameter is NOT "keep
    // whatever the swap chain already has" - passing 0 here would silently
    // drop DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING every time the window resizes,
    // even though the swap chain was created with it. Re-supply the same
    // flags used at creation (mSwapChainFlags), matching the pattern used in
    // Microsoft's own tearing-sample code.
    HRESULT hr = mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, mSwapChainFlags);
    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "ResizeBuffers failed" << LL_ENDL;
        return false;
    }

    mWidth = width;
    mHeight = height;
    return createBackBufferRTV() && createDepthStencilView();
}

ID3D11Texture2D* DXSwapChain::getBackBufferTexture() const
{
    if (!mBackBufferRTV)
    {
        return nullptr;
    }

    ID3D11Resource* resource = nullptr;
    mBackBufferRTV->GetResource(&resource);
    if (!resource)
    {
        return nullptr;
    }

    // GetResource() returns an ID3D11Resource (AddRef'd) - the RTV was
    // created directly from an ID3D11Texture2D in createBackBufferRTV()
    // above, so this QueryInterface always succeeds; still checked rather
    // than a raw static_cast, since a failed QI would otherwise return a
    // subtly-wrong pointer instead of a clean nullptr.
    ID3D11Texture2D* texture = nullptr;
    resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture);
    resource->Release();
    return texture;
}

void DXSwapChain::present()
{
    if (!mSwapChain)
    {
        return;
    }

    // S24 (task #210): DXGI_PRESENT_ALLOW_TEARING is only legal when VSync
    // is off AND the swap chain was actually created with
    // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING (mAllowTearing) - using the flag
    // otherwise is a documented DXGI error, not just a no-op.
    UINT present_flags = (!mVSync && mAllowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    mSwapChain->Present(mVSync ? 1 : 0, present_flags);

    // S24 (task #210): under flip model, GetBuffer(0,...) means "the
    // current back buffer," which rotates every Present() call - unlike the
    // old BitBlt model where index 0 was the same physical buffer forever.
    // Re-fetch it here, right after Present(), so it's correct before
    // DXContext::beginFrame() (called immediately after this, see
    // llwindowwin32.cpp) rebinds it for the next frame. No-op cost under
    // the BitBlt-model fallback path (mFlipModel false).
    if (mFlipModel)
    {
        releaseBackBufferRTV();
        createBackBufferRTV();
    }
}
