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
    // S24 (2026-08-10, task #147/#184 follow-up): flip_y ADDED - reflection-
    // probe cube-face capture (LLViewerWindow::cubeSnapshot()/
    // display_cube_face()) reuses GL's own canonical cubemap face look/up
    // vector table (LLCubeMapArray::sLookVecs/sUpVecs and
    // sClipToCubeLookVecs/sClipToCubeUpVecs) unconditionally for both
    // backends - correct for GL's own bottom-to-top texture-row convention,
    // but D3D11's TextureCube::Sample() expects each face stored top-to-
    // bottom, so content captured with GL's convention reads back
    // vertically flipped under D3D11 (confirmed - user reported reflection/
    // ambient probe content specifically appearing upside down, right after
    // the mip-generation copy pipeline was fixed for real - task #147/#184
    // - and could finally be seen clearly for the first time). Uses D3D11's
    // standard negative-height-viewport technique (TopLeftY shifted down by
    // height, Height negated) rather than touching any camera/direction
    // vector math - a single, isolated, easily-reversible flip at the
    // rasterizer level, gated to the one caller that needs it
    // (LLViewerWindow::setup3DViewport(), only when gCubeSnapshot is true).
    // Every other caller passes flip_y=false (default), completely
    // unaffected.
    void setViewport(int x, int y, int width, int height, bool flip_y = false);
};

extern DXContext gDXContext;
