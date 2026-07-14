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
};

extern DXContext gDXContext;
