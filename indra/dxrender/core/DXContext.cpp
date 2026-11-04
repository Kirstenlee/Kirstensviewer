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

    // D3D11's implicit default rasterizer state is CullMode=BACK, matching
    // GL's default of culling off - set explicit state matching GL here so
    // nothing relies on the D3D11 default.
    ctx->RSSetState(DXStateCache::getRasterizerState(false, false));

    const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (rtv)
    {
        ctx->ClearRenderTargetView(rtv, clear_color);
    }
    // Cleared here on the unused back buffer; world geometry draws into
    // mRT->deferredScreen/screen instead, until presentDeferredScreen()
    // rebinds the back buffer later this frame for 3D-in-UI-space content
    // (manipulator gizmos, selection highlight).
    if (dsv)
    {
        // 0.0f is "far" under the reversed-Z convention - see
        // kGLtoDXDepthRemap (llrender.cpp).
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
    // Standard D3D11 negative-height viewport flip, used for reflection-probe
    // cube-face capture. Shifting TopLeftY down by the full height and
    // negating Height mirrors the target vertically with no other state
    // (camera, projection, winding/cull) touched.
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
