#include "DXContext.h"
#include "DXDevice.h"
#include "DXSwapChain.h"

DXContext gDXContext;

void DXContext::beginFrame()
{
    ID3D11DeviceContext* ctx = gDXDevice.getContext();
    if (!ctx)
    {
        return;
    }

    ID3D11RenderTargetView* rtv = gDXSwapChain.getBackBufferRTV();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);

    // Distinct dark blue, not black, so it's visually obvious the DX11 path
    // is actually presenting rather than showing a blank/uninitialized window.
    const float clear_color[4] = { 0.0f, 0.0f, 0.2f, 1.0f };
    if (rtv)
    {
        ctx->ClearRenderTargetView(rtv, clear_color);
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
