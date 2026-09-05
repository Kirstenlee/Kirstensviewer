#include "DXContext.h"
#include "DXDevice.h"
#include "DXSwapChain.h"
#include "DXStateCache.h"

DXContext gDXContext;

void DXContext::beginFrame()
{
    ID3D11DeviceContext* ctx = gDXDevice.getContext();
    if (!ctx)
    {
        return;
    }

    ID3D11RenderTargetView* rtv = gDXSwapChain.getBackBufferRTV();
    ID3D11DepthStencilView* dsv = gDXSwapChain.getDepthStencilView();
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    DXStateCache::bumpRTVGeneration();

    // S24 (2026-07-23): D3D11's own built-in default rasterizer state (in
    // effect until the first explicit RSSetState() call) is CullMode=BACK,
    // FrontCounterClockwise=FALSE - GL's actual default is the opposite
    // (culling off; glEnable(GL_CULL_FACE) is opt-in and never called
    // implicitly). Any draw issued before something explicitly toggles
    // LLGLEnable/LLGLDisable(GL_CULL_FACE) - which is most 2D UI code, since
    // GL's default already matches "off" - ran under D3D11's raw default
    // instead: every CCW-wound (i.e. every, see getRasterizerState()'s
    // comment) triangle silently back-face culled, while line-mode geometry
    // (unaffected by face culling) rendered fine. Set an explicit known
    // state matching GL's real default at the start of every frame so nothing
    // relies on D3D11's implicit one.
    ctx->RSSetState(DXStateCache::getRasterizerState(false, false));

    // S24 (2026-07-22): was a distinct dark blue (0,0,0.2,1) as a Milestone-1
    // sanity check that the DX11 path was presenting at all - that job is
    // long done (confirmed by every subsequent frame of real content since),
    // and the leftover tint was actively misleading later "why is the screen
    // blue" visibility debugging. Neutral black now, matching GL's default.
    const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (rtv)
    {
        ctx->ClearRenderTargetView(rtv, clear_color);
    }
    // S24 (2026-08-09): see DXSwapChain::getDepthStencilView()'s comment -
    // cleared once here, at the top of the frame, while the back buffer is
    // still unused (all real world geometry draws into mRT->deferredScreen/
    // mRT->screen instead, not the back buffer, until presentDeferredScreen()
    // rebinds it later this same frame) - by the time 3D-in-UI-space content
    // (manipulator gizmos, selection highlight, etc.) draws against it,
    // it's still in this freshly-cleared state.
    if (dsv)
    {
        // S24 (reversed-Z conversion): 0.0f is now "far" - see
        // kGLtoDXDepthRemap's comment (llrender.cpp).
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
    }

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)gDXSwapChain.getWidth();
    vp.Height = (float)gDXSwapChain.getHeight();
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
}

void DXContext::endFrame()
{
    // No D3D11 equivalent of glFlush needed here - Present() itself lives on
    // DXSwapChain, invoked from LLWindowWin32::swapBuffers()'s DX_RENDER branch.
}

void DXContext::setViewport(int x, int y, int width, int height, bool flip_y)
{
    ID3D11DeviceContext* ctx = gDXDevice.getContext();
    if (!ctx)
    {
        return;
    }

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = (float)x;
    // S24 (2026-08-10, task #147/#184 follow-up): standard D3D11 negative-
    // height viewport flip - see this method's header comment for why
    // (reflection-probe cube-face capture specifically). Shifting TopLeftY
    // down by the full height and negating Height rasterizes the same
    // scene content mirrored vertically within the target, with no other
    // state (camera, projection, winding/cull) touched.
    if (flip_y)
    {
        vp.TopLeftY = (float)(y + height);
        vp.Height = -(float)height;
    }
    else
    {
        vp.TopLeftY = (float)y;
        vp.Height = (float)height;
    }
    vp.Width = (float)width;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
}
