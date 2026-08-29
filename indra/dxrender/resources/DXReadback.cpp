#include "DXReadback.h"
#include "DXDevice.h"
#include "llerror.h"
#include <cstdint>
#include <cstring>

namespace
{
    // Shared setup for both readPixels()/readDepthPixels(): creates a
    // STAGING texture sized to (width,height) matching src's format,
    // copies the requested region into it, and maps it for CPU read.
    // Returns the (still-mapped) staging texture via `out_staging` and the
    // mapped subresource via `out_mapped` on success - caller owns
    // Unmap()+Release() on `out_staging`. Returns false (nothing to clean
    // up) on failure.
    bool stageAndMap(ID3D11Texture2D* src, int x, int y, int width, int height,
        ID3D11Texture2D** out_staging, D3D11_MAPPED_SUBRESOURCE* out_mapped)
    {
        if (!src || width <= 0 || height <= 0)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC src_desc = {};
        src->GetDesc(&src_desc);

        D3D11_TEXTURE2D_DESC staging_desc = src_desc;
        staging_desc.Width = (UINT)width;
        staging_desc.Height = (UINT)height;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;
        staging_desc.MipLevels = 1;
        staging_desc.ArraySize = 1;
        staging_desc.SampleDesc.Count = 1;
        staging_desc.SampleDesc.Quality = 0;

        ID3D11Texture2D* staging = nullptr;
        HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&staging_desc, nullptr, &staging);
        if (FAILED(hr))
        {
            LL_WARNS("Readback") << "DXReadback: CreateTexture2D(STAGING) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
            return false;
        }

        D3D11_BOX box = {};
        box.left = (UINT)x;
        box.top = (UINT)y;
        box.front = 0;
        box.right = (UINT)(x + width);
        box.bottom = (UINT)(y + height);
        box.back = 1;

        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, src, 0, &box);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
        {
            LL_WARNS("Readback") << "DXReadback: Map failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
            staging->Release();
            return false;
        }

        *out_staging = staging;
        *out_mapped = mapped;
        return true;
    }
}

bool DXReadback::readPixels(ID3D11Texture2D* src, int x, int y, int width, int height, int bytes_per_pixel, void* out_data)
{
    if (!out_data || bytes_per_pixel <= 0)
    {
        return false;
    }

    ID3D11Texture2D* staging = nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (!stageAndMap(src, x, y, width, height, &staging, &mapped))
    {
        return false;
    }

    const size_t row_bytes = (size_t)width * bytes_per_pixel;
    uint8_t* dst = (uint8_t*)out_data;
    const uint8_t* src_bytes = (const uint8_t*)mapped.pData;
    for (int row = 0; row < height; ++row)
    {
        memcpy(dst + (size_t)row * row_bytes, src_bytes + (size_t)row * mapped.RowPitch, row_bytes);
    }

    gDXDevice.getContext()->Unmap(staging, 0);
    staging->Release();
    return true;
}

bool DXReadback::writePixels(ID3D11Texture2D* dst, int x, int y, int width, int height, int bytes_per_pixel, const void* data)
{
    if (!dst || !data || width <= 0 || height <= 0 || bytes_per_pixel <= 0)
    {
        return false;
    }

    D3D11_BOX box = {};
    box.left = (UINT)x;
    box.top = (UINT)y;
    box.front = 0;
    box.right = (UINT)(x + width);
    box.bottom = (UINT)(y + height);
    box.back = 1;

    const UINT row_pitch = (UINT)width * (UINT)bytes_per_pixel;
    gDXDevice.getContext()->UpdateSubresource(dst, 0, &box, data, row_pitch, 0);
    return true;
}

bool DXReadback::readDepthPixels(ID3D11Texture2D* src, int x, int y, int width, int height, float* out_data)
{
    if (!out_data)
    {
        return false;
    }

    ID3D11Texture2D* staging = nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (!stageAndMap(src, x, y, width, height, &staging, &mapped))
    {
        return false;
    }

    // DXGI_FORMAT_D24_UNORM_S8_UINT packs one texel into 32 bits: bits
    // 0-23 = normalized depth, bits 24-31 = stencil (discarded here) -
    // see DXRenderTarget::allocateDepth(), the only depth format this
    // codebase creates.
    constexpr uint32_t DEPTH_MASK = 0x00FFFFFFu;
    constexpr float DEPTH_SCALE = 1.0f / (float)DEPTH_MASK;

    for (int row = 0; row < height; ++row)
    {
        const uint32_t* src_row = (const uint32_t*)((const uint8_t*)mapped.pData + (size_t)row * mapped.RowPitch);
        float* dst_row = out_data + (size_t)row * width;
        for (int col = 0; col < width; ++col)
        {
            dst_row[col] = (float)(src_row[col] & DEPTH_MASK) * DEPTH_SCALE;
        }
    }

    gDXDevice.getContext()->Unmap(staging, 0);
    staging->Release();
    return true;
}
