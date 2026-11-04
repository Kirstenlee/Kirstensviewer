#pragma once

// Per-frame bracket for the DX_RENDER backend - the analogue of what GL does
// implicitly via wglMakeCurrent/SwapBuffers. beginFrame() binds and clears
// the back buffer; the actual Present() lives on DXSwapChain, invoked from
// LLWindowWin32::swapBuffers()'s DX_RENDER branch.
class DXContext
{
public:
    void beginFrame();
    void endFrame();

    // Unlike GL (which has an implicit full-window default), D3D11 always
    // needs an explicit viewport - beginFrame() sets one matching the swap
    // chain's full back-buffer size, but callers rendering into a
    // different-sized target (e.g. LLViewerWindow::setup3DViewport() during
    // reflection-probe cube-face capture, a smaller square render target)
    // need to override it. Real, cheap DX11-native behavior, not a skip -
    // unlike line-width/polygon-offset, D3D11 has a direct viewport
    // equivalent.
    //
    // flip_y: LLCubeMapArray's look/up vector tables assume GL's bottom-to-
    // top cubemap face row convention, but D3D11's TextureCube::Sample()
    // expects top-to-bottom, so cube-face capture reads back vertically
    // flipped under D3D11. Fixed via a negative-height viewport flip at the
    // rasterizer level (TopLeftY shifted down by height, Height negated)
    // instead of touching camera/direction math - gated to
    // LLViewerWindow::setup3DViewport() when gCubeSnapshot is true; every
    // other caller passes flip_y=false.
    void setViewport(int x, int y, int width, int height, bool flip_y = false);
};

extern DXContext gDXContext;
