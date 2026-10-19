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
    // D3D11_BIND_CONSTANT_BUFFER factory alongside the vertex/index ones
    // above, reusing create()'s already-generic body. Used for
    // LLSphereProbes's ReflectionProbeData UBO equivalent; upload()
    // below is reused as-is for the per-frame re-upload.
    bool createConstantBuffer(size_t size, const void* data);
    void destroy();

    // Re-uploads the full buffer via Map(WRITE_DISCARD)/Unmap. `size` must
    // match the size passed to create*Buffer().
    bool upload(const void* data, size_t size);

    ID3D11Buffer* getBuffer() const { return mBuffer; }

private:
    bool create(size_t size, const void* data, UINT bind_flags);

    ID3D11Buffer* mBuffer = nullptr;
    // Needed by destroy() to return this buffer to the right pool bucket -
    // see DXBufferPool's comment in the .cpp for why pooling exists at all.
    size_t mSize = 0;
    UINT mBindFlags = 0;
};
