#include "DXSwapChain.h"
#include "DXDevice.h"
#include "llerror.h"

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
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = factory->CreateSwapChain(device, &desc, &mSwapChain);
    factory->Release();

    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "CreateSwapChain failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    mWidth = width;
    mHeight = height;
    mVSync = vsync;

    return createBackBufferRTV();
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
    if (mBackBufferRTV) { mBackBufferRTV->Release(); mBackBufferRTV = nullptr; }
    if (mSwapChain) { mSwapChain->Release(); mSwapChain = nullptr; }
}

bool DXSwapChain::resize(int width, int height)
{
    if (!mSwapChain)
    {
        return false;
    }

    if (mBackBufferRTV) { mBackBufferRTV->Release(); mBackBufferRTV = nullptr; }

    HRESULT hr = mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "ResizeBuffers failed" << LL_ENDL;
        return false;
    }

    mWidth = width;
    mHeight = height;
    return createBackBufferRTV();
}

void DXSwapChain::present()
{
    if (mSwapChain)
    {
        mSwapChain->Present(mVSync ? 1 : 0, 0);
    }
}
