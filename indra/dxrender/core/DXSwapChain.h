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

    // Owned and sized by the swap chain itself (matching a GL default
    // framebuffer's own bundled depth buffer) so 3D-in-UI-space content
    // (manipulator gizmos, selection highlight, coordinate axes, tracking
    // beacons - drawn after DXPipeline::presentDeferredScreen() rebinds the
    // back buffer) has a real DSV to depth-test against; without one,
    // DepthEnable=TRUE via LLGLDepthTest with no DSV bound makes D3D11
    // silently drop those draw calls. Cleared once per frame in
    // DXContext::beginFrame(). Does not yet test against real world/avatar
    // depth (would need a depth copy from the deferred G-buffer).
    ID3D11DepthStencilView* getDepthStencilView() const { return mDepthStencilView; }

    // Returns the back buffer's underlying texture (e.g. for DXReadback -
    // GL's glReadPixels() with no explicit target reads from whatever's
    // currently the default framebuffer, which for this codebase's single-
    // window viewer is always the swap chain back buffer). COM convention:
    // AddRef's the returned pointer via GetResource() - caller must
    // Release() it. Returns nullptr if the swap chain isn't created.
    ID3D11Texture2D* getBackBufferTexture() const;

    int getWidth() const { return mWidth; }
    int getHeight() const { return mHeight; }

    // Live VSync toggle - present() reads mVSync fresh every call, so
    // changing it here takes effect on the very next frame with no swap
    // chain recreation needed. See LLWindowWin32::toggleVSync()'s DX_RENDER
    // branch.
    void setVSync(bool vsync) { mVSync = vsync; }
    bool getVSync() const { return mVSync; }

    // Set by present() when Present() fails with DXGI_ERROR_DEVICE_REMOVED/DEVICE_RESET (TDR,
    // driver crash/update, eGPU unplug, GPU switch on a hybrid-graphics laptop). No recovery is
    // attempted - every further D3D11 call on this device will also fail - the caller
    // (llwindowwin32.cpp) checks this right after present() and terminates gracefully with a
    // real error message instead of silently rendering garbage forever.
    bool isDeviceLost() const { return mDeviceLost; }

private:
    bool createBackBufferRTV();
    bool createDepthStencilView();
    void releaseDepthStencilView();
    void releaseBackBufferRTV();

    IDXGISwapChain* mSwapChain = nullptr;
    ID3D11RenderTargetView* mBackBufferRTV = nullptr;
    ID3D11Texture2D* mDepthStencilTexture = nullptr;
    ID3D11DepthStencilView* mDepthStencilView = nullptr;
    int mWidth = 0;
    int mHeight = 0;
    bool mVSync = true;

    // Under flip model (DXGI_SWAP_EFFECT_FLIP_DISCARD), GetBuffer(0,...)
    // means "whichever buffer is currently the back buffer" and rotates
    // every Present() call, unlike BitBlt model where index 0 is the same
    // physical buffer forever. mBackBufferRTV is re-fetched every frame in
    // present() (right after the real Present() call) whenever mFlipModel is
    // true. mDepthStencilTexture/View are NOT affected - that's a private
    // texture this class allocates itself, never obtained via GetBuffer.
    bool mFlipModel = false;
    bool mAllowTearing = false;
    UINT mSwapChainFlags = 0;
    bool mDeviceLost = false;
};

extern DXSwapChain gDXSwapChain;
