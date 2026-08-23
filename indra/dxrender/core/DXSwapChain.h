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

    // S24 (2026-08-09): the swap chain never had a depth-stencil buffer of
    // its own - DXContext::beginFrame() and DXRenderTarget::
    // bindSwapChainBackBuffer() both bound the back buffer with a null DSV,
    // which was fine as long as nothing drawn directly onto the back buffer
    // needed real depth testing (all real 3D world geometry goes through
    // mRT->deferredScreen/mRT->screen instead, which do have their own real
    // depth buffers). That stopped being true once 3D-in-UI-space content
    // (LLManipTranslate's arrow gizmos, LLSelectNode::renderOneSilhouette()'s
    // selection highlight, coordinate axes, tracking beacons - everything
    // LLViewerWindow::renderSelections()/render_hud_elements() draws, all
    // AFTER DXPipeline::presentDeferredScreen() has already rebound the back
    // buffer) turned out to need it - with DepthEnable=TRUE requested via
    // LLGLDepthTest but no depth-stencil view ever bound, D3D11 silently
    // dropped every one of those draw calls (confirmed: user reported the
    // whole 3D-in-UI-space pass - arrows, highlight, axes, beacons -
    // universally invisible, not just one of them). One depth buffer, owned
    // and sized by the swap chain itself (matching a GL default framebuffer's
    // own bundled depth buffer), fixes this at both binding chokepoints at
    // once. Cleared once per frame in DXContext::beginFrame() - by the time
    // this pass runs later in the same frame nothing else has touched it
    // (the deferred lighting/present passes run with depth test off, see
    // their own comments), so depth ordering among this pass's own elements
    // works even though it doesn't yet test against real world/avatar depth
    // (that would need a depth copy from the deferred G-buffer - a separate,
    // more involved follow-up, not required just to make this pass visible
    // again).
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

    // S24 (2026-08-16): live VSync toggle - unlike GL's wglSwapIntervalEXT
    // dance (needs a current context, a probed extension function pointer),
    // present() already reads mVSync fresh every call, so changing it here
    // takes effect on the very next frame with zero extra work - no swap
    // chain recreation, no context requirement. See LLWindowWin32::
    // toggleVSync()'s DX_RENDER branch, the only caller (RenderVSyncEnable
    // changing live in Preferences).
    void setVSync(bool vsync) { mVSync = vsync; }
    bool getVSync() const { return mVSync; }

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

    // S24 (task #210): flip-model (DXGI_SWAP_EFFECT_FLIP_DISCARD) is
    // required for DXGI_PRESENT_ALLOW_TEARING and is a strict upgrade over
    // the old BitBlt model (DXGI_SWAP_EFFECT_DISCARD) even when tearing
    // itself isn't used - but it changes GetBuffer(0,...) semantics: under
    // BitBlt model, buffer index 0 is the SAME physical buffer forever, so
    // fetching mBackBufferRTV once at create()/resize() time and reusing it
    // every frame (the old behavior) was correct. Under flip model, index 0
    // is defined as "whichever buffer is currently the back buffer" and
    // rotates every Present() call - a cached RTV goes stale on frame 2.
    // mBackBufferRTV is now re-fetched every frame (present(), right after
    // the real Present() call, so it's ready before beginFrame() asks for
    // it next) whenever mFlipModel is true. mDepthStencilTexture/View are
    // NOT affected - that's a private texture this class allocates itself
    // (never obtained via the swap chain's GetBuffer), so it never rotates
    // regardless of swap effect.
    bool mFlipModel = false;
    bool mAllowTearing = false;
    UINT mSwapChainFlags = 0;
};

extern DXSwapChain gDXSwapChain;
