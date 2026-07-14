#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

// Wraps the IDXGISwapChain + back-buffer render target view used by the
// DX_RENDER backend. One swap chain per window (this viewer has one window).
class DXSwapChain
{
public:
    bool create(HWND hwnd, int width, int height, bool vsync);
    void destroy();
    bool resize(int width, int height);
    void present();

    ID3D11RenderTargetView* getBackBufferRTV() const { return mBackBufferRTV; }
    int getWidth() const { return mWidth; }
    int getHeight() const { return mHeight; }

private:
    bool createBackBufferRTV();

    IDXGISwapChain* mSwapChain = nullptr;
    ID3D11RenderTargetView* mBackBufferRTV = nullptr;
    int mWidth = 0;
    int mHeight = 0;
    bool mVSync = true;
};

extern DXSwapChain gDXSwapChain;
