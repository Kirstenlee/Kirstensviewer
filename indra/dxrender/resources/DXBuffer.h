#pragma once
#include <d3d11.h>

// Wraps one D3D11_USAGE_DYNAMIC vertex or index buffer. LLVertexBuffer keeps
// its own CPU-side shadow copy (mMappedData/mMappedIndexData - plain heap
// memory, unrelated to any D3D11 mapping) and this class only owns the
// GPU-side resource plus the Map/Unmap bridge that pushes it - there is no
// D3D11 equivalent of a partial-range glBufferSubData on a DYNAMIC buffer
// (Map(WRITE_DISCARD) invalidates the whole resource), so upload() always
// re-uploads the full buffer; see LLVertexBuffer::flush_vbo()'s DX_RENDER
// branch for why that's correct given mMappedData is always fully populated.
class DXBuffer
{
public:
    ~DXBuffer() { destroy(); }

    bool createVertexBuffer(size_t size, const void* data);
    bool createIndexBuffer(size_t size, const void* data);
    // S24 (2026-08-09, task #147b): real D3D11 constant buffer support -
    // create()'s D3D11_USAGE_DYNAMIC/D3D11_CPU_ACCESS_WRITE body was already
    // generic (bind_flags is a parameter), this just exposes a
    // D3D11_BIND_CONSTANT_BUFFER factory alongside the existing two. Used
    // for LLReflectionMapManager's ReflectionProbeData UBO equivalent -
    // upload() (below) is reused as-is for the per-frame re-upload, same
    // Map(WRITE_DISCARD) shape GL's glBufferData(..., GL_STREAM_DRAW) has.
    bool createConstantBuffer(size_t size, const void* data);
    void destroy();

    // Re-uploads the full buffer via Map(WRITE_DISCARD)/Unmap. `size` must
    // match the size passed to create*Buffer().
    bool upload(const void* data, size_t size);

    ID3D11Buffer* getBuffer() const { return mBuffer; }

private:
    bool create(size_t size, const void* data, UINT bind_flags);

    ID3D11Buffer* mBuffer = nullptr;
};
