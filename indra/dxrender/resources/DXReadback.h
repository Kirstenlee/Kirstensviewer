#pragma once
#include <d3d11.h>

// One-shot GPU->CPU pixel readback via a D3D11_USAGE_STAGING texture - the
// D3D11 equivalent of GL's glReadPixels(), which has no direct counterpart
// (no CPU-readable "currently rendered" surface exists in D3D11). Wraps the
// standard idiom: CreateTexture2D(STAGING) sized to the requested region +
// CopySubresourceRegion from the GPU-resident source + Map(READ) + memcpy
// row-by-row (respecting RowPitch, which can differ from
// width*bytes_per_pixel due to driver alignment) + Unmap.
//
// Deliberately creates a fresh staging texture per call rather than
// caching/reusing one - correctness first, matching this project's already-
// established "no memoization yet, can optimize later if profiling shows
// it matters" precedent (DXBuffer::upload() always re-uploads the full
// buffer; LLRender::syncMatrices() has no hash-based memoization). Not a
// hot path today - called once per UI frame at most (debug color picker)
// or a handful of times total during snapshot capture.
class DXReadback
{
public:
    // Reads a `width` x `height` region starting at (`x`,`y`) - D3D11's
    // native top-left-origin row order, NOT GL's bottom-left origin;
    // callers translating a GL-style (x,y) must flip y themselves (see
    // llviewerwindow.cpp's call sites for the exact translation) - from
    // `src` (any GPU-resident ID3D11Texture2D: a render target's color
    // attachment, the swap chain's back buffer, etc.) into `out_data`,
    // tightly packed (no row padding), `bytes_per_pixel` bytes per pixel.
    // `src` must already be in a format whose per-texel byte size matches
    // `bytes_per_pixel` - this does no format conversion of its own.
    // Returns false on any failure (logs via LL_WARNS).
    static bool readPixels(ID3D11Texture2D* src, int x, int y, int width, int height, int bytes_per_pixel, void* out_data);

    // Depth-specific variant: reads the same region from a
    // DXGI_FORMAT_D24_UNORM_S8_UINT depth-stencil texture (the only depth
    // format this codebase creates - see DXRenderTarget::allocateDepth())
    // and unpacks each texel's 24-bit normalized depth into a float in
    // [0,1] (discarding the 8-bit stencil), written to `out_data` as
    // tightly-packed floats - matching what GL's
    // glReadPixels(..., GL_DEPTH_COMPONENT, GL_FLOAT, ...) produces.
    static bool readDepthPixels(ID3D11Texture2D* src, int x, int y, int width, int height, float* out_data);

    // S24 (2026-08-17): mirror of readPixels() for the opposite direction -
    // the D3D11 equivalent of glTexSubImage2D(). Writes `width`x`height`
    // tightly-packed pixel data starting at (`x`,`y`) (same top-left-origin
    // convention as readPixels()) into `dst` (any D3D11_USAGE_DEFAULT
    // ID3D11Texture2D - render target color attachments included;
    // UpdateSubresource() doesn't require the resource to be unbound
    // first). No staging texture needed for this direction - unlike the
    // read side, D3D11 lets you write directly to a DEFAULT-usage resource
    // from CPU memory in one call.
    static bool writePixels(ID3D11Texture2D* dst, int x, int y, int width, int height, int bytes_per_pixel, const void* data);
};
