#include "DXBuffer.h"
#include "DXDevice.h"
#include "llerror.h"
#include <chrono>
#include <cstdint>
#include <iterator>
#include <list>
#include <unordered_map>

namespace
{
    // S24: every LLSpatialGroup rebuild destroys its old vertex/index
    // buffers and creates brand-new ones (see LLVertexBuffer::genBuffer()/
    // genIndices(), destroyGLBuffer()/destroyGLIndices()) - there was no
    // reuse at all under DX_RENDER (the legacy GL LLVBOPool is explicitly
    // bypassed, see LLVertexBuffer::initClass()'s comment - it's GL-driver-
    // quirk-shaped, not reusable for a raw ID3D11Buffer*). Entering a region
    // or turning into a dense area marks many groups dirty simultaneously,
    // meaning many simultaneous CreateBuffer() calls - this pool recycles
    // same-size buffers instead, exact-size-bucketed (no slack/rounding) and
    // keyed separately per bind type (vertex vs index - constant buffers are
    // few, long-lived, and not part of this churn, so they stay unpooled).
    // Primarily helps steady-state LOD/geometry churn and revisited areas
    // (a freshly-never-seen region's first load is still a genuine cold
    // miss, pool or not) - this is the same class of fix as DXTexture's
    // media DYNAMIC-texture change, just at the allocation-reuse level
    // instead of the update level.
    class DXBufferPool
    {
    public:
        using Clock = std::chrono::steady_clock;

        ID3D11Buffer* take(UINT bind_flags, size_t size)
        {
            auto& pool = bucketFor(bind_flags);
            auto iter = pool.find(size);
            if (iter == pool.end() || iter->second.empty())
            {
                return nullptr;
            }

            ID3D11Buffer* buffer = iter->second.back().mBuffer;
            iter->second.pop_back();
            if (iter->second.empty())
            {
                pool.erase(iter);
            }
            return buffer;
        }

        void give(UINT bind_flags, size_t size, ID3D11Buffer* buffer)
        {
            auto& pool = bucketFor(bind_flags);
            std::list<Entry>& entries = pool[size];

            // Cap entries per exact-size bucket - an unbounded free list
            // would just leak VRAM for one-off large allocations that never
            // recur at that exact size.
            if (entries.size() >= MAX_ENTRIES_PER_SIZE)
            {
                buffer->Release();
                return;
            }

            entries.push_front({ buffer, Clock::now() });
            clean();
        }

    private:
        struct Entry
        {
            ID3D11Buffer* mBuffer;
            Clock::time_point mAge;
        };
        using Pool = std::unordered_map<size_t, std::list<Entry>>;

        static constexpr size_t MAX_ENTRIES_PER_SIZE = 4;

        Pool& bucketFor(UINT bind_flags)
        {
            return bind_flags == D3D11_BIND_INDEX_BUFFER ? mIndexPool : mVertexPool;
        }

        // Evicts entries idle for 5+ seconds, same shape/threshold as the
        // legacy GL LLDefaultVBOPool's clean() - checked every call rather
        // than on a timer, cheap given this only runs on destroy()/give().
        void clean()
        {
            if (++mTouchCount < 1024)
            {
                return;
            }
            mTouchCount = 0;

            const Clock::time_point cutoff = Clock::now() - std::chrono::seconds(5);
            for (Pool* pool : { &mVertexPool, &mIndexPool })
            {
                for (auto iter = pool->begin(); iter != pool->end(); )
                {
                    std::list<Entry>& entries = iter->second;
                    while (!entries.empty() && entries.back().mAge < cutoff)
                    {
                        entries.back().mBuffer->Release();
                        entries.pop_back();
                    }
                    iter = entries.empty() ? pool->erase(iter) : std::next(iter);
                }
            }
        }

        Pool mVertexPool;
        Pool mIndexPool;
        uint32_t mTouchCount = 0;
    };

    DXBufferPool sDXBufferPool;
}

bool DXBuffer::create(size_t size, const void* data, UINT bind_flags)
{
    if (mBuffer)
    {
        destroy();
    }

    if (size == 0)
    {
        return true;
    }

    // Constant buffers aren't pooled (see DXBufferPool's comment) - only
    // take the pool path for vertex/index buffers.
    if (bind_flags == D3D11_BIND_VERTEX_BUFFER || bind_flags == D3D11_BIND_INDEX_BUFFER)
    {
        mBuffer = sDXBufferPool.take(bind_flags, size);
        if (mBuffer)
        {
            mSize = size;
            mBindFlags = bind_flags;
            if (data)
            {
                // Pooled buffers never arrive pre-populated - callers that
                // pass real initial data (unlike genBuffer()/genIndices(),
                // which always pass nullptr) still need it pushed via
                // Map(DISCARD), same mechanism as upload().
                if (!upload(data, size))
                {
                    destroy();
                    return false;
                }
            }
            return true;
        }
    }

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (UINT)size;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = bind_flags;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = data;

    HRESULT hr = gDXDevice.getDevice()->CreateBuffer(&desc, data ? &init_data : nullptr, &mBuffer);
    if (FAILED(hr))
    {
        LL_WARNS("VertexBuffer") << "CreateBuffer failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    mSize = size;
    mBindFlags = bind_flags;
    return true;
}

bool DXBuffer::createVertexBuffer(size_t size, const void* data)
{
    return create(size, data, D3D11_BIND_VERTEX_BUFFER);
}

bool DXBuffer::createIndexBuffer(size_t size, const void* data)
{
    return create(size, data, D3D11_BIND_INDEX_BUFFER);
}

bool DXBuffer::createConstantBuffer(size_t size, const void* data)
{
    return create(size, data, D3D11_BIND_CONSTANT_BUFFER);
}

void DXBuffer::destroy()
{
    if (mBuffer)
    {
        if (mBindFlags == D3D11_BIND_VERTEX_BUFFER || mBindFlags == D3D11_BIND_INDEX_BUFFER)
        {
            // Hands the buffer back to the pool instead of releasing it -
            // see DXBufferPool's comment. give() itself falls back to
            // Release() if that size bucket is already full.
            sDXBufferPool.give(mBindFlags, mSize, mBuffer);
        }
        else
        {
            mBuffer->Release();
        }
        mBuffer = nullptr;
        mSize = 0;
        mBindFlags = 0;
    }
}

bool DXBuffer::upload(const void* data, size_t size)
{
    if (!mBuffer)
    {
        return false;
    }

    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = ctx->Map(mBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
    {
        LL_WARNS("VertexBuffer") << "Map failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    memcpy(mapped.pData, data, size);
    ctx->Unmap(mBuffer, 0);
    return true;
}
