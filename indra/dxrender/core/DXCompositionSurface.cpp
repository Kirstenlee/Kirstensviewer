#include "DXCompositionSurface.h"
#include "DXDevice.h"

#include <dxgi1_2.h>
#include "llerror.h"

bool DXCompositionSurface::createForWindow(HWND host, UINT width, UINT height)
{
    destroy();

    ID3D11Device* device = gDXDevice.getDevice();
    if (!device || !host)
    {
        LL_WARNS("DXRender") << "DXCompositionSurface::createForWindow called before DXDevice/host window is ready" << LL_ENDL;
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr) || !dxgiDevice)
    {
        LL_WARNS("DXRender") << "Failed to query IDXGIDevice from ID3D11Device for DirectComposition, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    hr = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), (void**)&mDevice);
    dxgiDevice->Release();
    if (FAILED(hr) || !mDevice)
    {
        LL_WARNS("DXRender") << "DCompositionCreateDevice failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    hr = mDevice->CreateTargetForHwnd(host, TRUE, &mTarget);
    if (FAILED(hr) || !mTarget)
    {
        LL_WARNS("DXRender") << "IDCompositionDevice::CreateTargetForHwnd failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        destroy();
        return false;
    }

    hr = mDevice->CreateVisual(&mVisual);
    if (FAILED(hr) || !mVisual)
    {
        LL_WARNS("DXRender") << "IDCompositionDevice::CreateVisual failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        destroy();
        return false;
    }

    if (!createSurface(width, height))
    {
        destroy();
        return false;
    }

    mVisual->SetContent(mSurface);
    mTarget->SetRoot(mVisual);
    mDevice->Commit();

    return true;
}

bool DXCompositionSurface::createSurface(UINT width, UINT height)
{
    releaseSurface();

    // DXGI_FORMAT_B8G8R8A8_UNORM - CreateSurface only accepts a small fixed
    // set of formats and rejects DXGI_FORMAT_R8G8B8A8_UNORM outright
    // (E_INVALIDARG, confirmed live) - the source LLRenderTarget is
    // allocated with GL_BGRA (llfloaterpopout.cpp) to match, so this is a
    // same-format copy, not a conversion.
    //
    // DXGI_ALPHA_MODE_PREMULTIPLIED - the most broadly-supported CreateSurface
    // alpha mode. The offscreen target is actually rendered with straight
    // (non-premultiplied) alpha (standard src_alpha/inv_src_alpha UI
    // blending, BT_ALPHA) - declaring PREMULTIPLIED without pre-multiplying
    // is a known, non-blocking mismatch (only semi-transparent pixels look
    // slightly off; fully opaque UI is unaffected) - fine for proving basic
    // visibility/positioning; can be corrected with a real premultiply pass
    // later if the live test shows a translucency artifact.
    HRESULT hr = mDevice->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_ALPHA_MODE_PREMULTIPLIED, &mSurface);
    if (FAILED(hr) || !mSurface)
    {
        LL_WARNS("DXRender") << "IDCompositionDevice::CreateSurface failed (" << width << "x" << height
            << "), hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mSurface = nullptr;
        return false;
    }

    mWidth = width;
    mHeight = height;
    return true;
}

void DXCompositionSurface::releaseSurface()
{
    if (mSurface)
    {
        mSurface->Release();
        mSurface = nullptr;
    }
    mWidth = 0;
    mHeight = 0;
}

void DXCompositionSurface::update(ID3D11Texture2D* src, UINT width, UINT height)
{
    if (!mDevice || !mVisual || !src)
    {
        return;
    }

    if (width != mWidth || height != mHeight)
    {
        if (!createSurface(width, height))
        {
            return;
        }
        mVisual->SetContent(mSurface);
    }

    POINT offset = {};
    IDXGISurface* dxgiSurface = nullptr;
    HRESULT hr = mSurface->BeginDraw(nullptr, __uuidof(IDXGISurface), (void**)&dxgiSurface, &offset);
    if (FAILED(hr) || !dxgiSurface)
    {
        LL_WARNS("DXRender") << "IDCompositionSurface::BeginDraw failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return;
    }

    ID3D11Texture2D* dstTex = nullptr;
    if (SUCCEEDED(dxgiSurface->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&dstTex)) && dstTex)
    {
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        D3D11_BOX box = {};
        box.left = 0; box.top = 0; box.front = 0;
        box.right = width; box.bottom = height; box.back = 1;
        ctx->CopySubresourceRegion(dstTex, 0, (UINT)offset.x, (UINT)offset.y, 0, src, 0, &box);
        dstTex->Release();
    }
    dxgiSurface->Release();

    mSurface->EndDraw();
    mDevice->Commit();
}

void DXCompositionSurface::setPosition(int x, int y)
{
    if (!mVisual || !mDevice)
    {
        return;
    }
    mVisual->SetOffsetX((float)x);
    mVisual->SetOffsetY((float)y);
    mDevice->Commit();
}

void DXCompositionSurface::destroy()
{
    releaseSurface();
    if (mVisual) { mVisual->Release(); mVisual = nullptr; }
    if (mTarget) { mTarget->Release(); mTarget = nullptr; }
    if (mDevice) { mDevice->Release(); mDevice = nullptr; }
}
