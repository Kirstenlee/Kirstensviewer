#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>

// Stage B of the floater-outside-viewport framework (inherited-tickling-
// kurzweil plan: render-to-texture + DirectComposition). Wraps a
// DirectComposition device/target/visual/surface so an already-rendered
// D3D11 texture (rendered on the existing single device/context - see
// LLFloaterPopoutManager::renderPoppedOut()) can be shown by DWM as an
// independently-positioned, alpha-composited visual hosted on a thin,
// input-only HWND. No second swap chain, no second D3D11 device - update()
// is a plain same-device texture copy, called from the main render thread.
//
// Plain UINT/int rather than indra's U32/S32 - this header is included
// before any llcommon header pulls those typedefs in (same convention as
// DXSwapChain.h's create(HWND, int, int, bool)).
class DXCompositionSurface
{
public:
    // host must be created with WS_EX_NOREDIRECTIONBITMAP. Creates the
    // composition device (once, QI'd from gDXDevice's existing ID3D11Device)
    // and a surface sized to width/height.
    bool createForWindow(HWND host, UINT width, UINT height);

    void destroy();

    // Copies src into the composition surface (resizing the surface first
    // if width/height changed) and commits. Main render thread only - src
    // must be a texture already rendered via the existing device/context
    // this frame (e.g. LLRenderTarget::getDXColorTexture()).
    void update(ID3D11Texture2D* src, UINT width, UINT height);

    // Position of the visual's top-left corner, in the host window's
    // desktop-relative coordinates.
    void setPosition(int x, int y);

    bool isValid() const { return mDevice != nullptr; }

private:
    bool createSurface(UINT width, UINT height);
    void releaseSurface();

    IDCompositionDevice* mDevice = nullptr;
    IDCompositionTarget* mTarget = nullptr;
    IDCompositionVisual* mVisual = nullptr;
    IDCompositionSurface* mSurface = nullptr;
    UINT mWidth = 0;
    UINT mHeight = 0;
};
