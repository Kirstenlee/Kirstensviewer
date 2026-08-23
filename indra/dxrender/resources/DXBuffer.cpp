#include "DXBuffer.h"
#include "DXDevice.h"
#include "llerror.h"

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
        mBuffer->Release();
        mBuffer = nullptr;
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
