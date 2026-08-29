#include "DXCubeArrayTexture.h"
#include "DXDevice.h"
#include "llerror.h"

bool DXCubeArrayTexture::create(int width, int height, int count, bool hdr, bool generate_mips)
{
    if (width <= 0 || height <= 0 || count <= 0)
    {
        return false;
    }

    destroy();
    mGenerateMips = generate_mips;
    mArraySize = (UINT)count * 6;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.ArraySize = mArraySize;
    // S24 (2026-08-09, task #147 step 2): D3D11 has no native 3-channel
    // float format - GL's hdr path uses GL_R11F_G11F_B10F for
    // components==3 (llcubemaparray.cpp), same reason DXTexture::create()
    // always expands to a 4-channel format for its own hdr case. Uses
    // R16G16B16A16_FLOAT rather than R11G11B10_FLOAT specifically because
    // its D3D11_FORMAT_SUPPORT_MIP_AUTOGEN is guaranteed at feature-level
    // 11 baseline - R11G11B10_FLOAT's isn't universally, and this array is
    // always created with generate_mips=true in practice (the reflection
    // probe manager relies on a real mip chain), so this avoids a
    // GenerateMips() capability gamble on lower-end hardware.
    desc.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    // Same two-step generate_mips pattern as DXCubeTexture::create(): a mip
    // chain requires D3D11_BIND_RENDER_TARGET + GENERATE_MIPS at creation
    // time, and MipLevels=0 requests the full auto chain down to 1x1 - the
    // real level count is queried back below via GetDesc(), needed to
    // compute correct subresource indices in copySliceFromBoundRenderTarget().
    if (generate_mips)
    {
        desc.MipLevels = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }
    else
    {
        desc.MipLevels = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    }

    HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&desc, nullptr, &mTexture);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXCubeArrayTexture::create: CreateTexture2D failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    D3D11_TEXTURE2D_DESC actual_desc = {};
    mTexture->GetDesc(&actual_desc);
    mMipLevels = actual_desc.MipLevels;

    // D3D11_SRV_DIMENSION_TEXTURECUBEARRAY, not TEXTURECUBE -
    // CreateShaderResourceView(mTexture, nullptr, &mSRV) (the nullptr-desc
    // auto-detect DXCubeTexture uses for its single-cubemap case) can't
    // infer array-vs-single-cube intent from ArraySize alone here, so this
    // needs an explicit desc, unlike DXCubeTexture::create().
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    srv_desc.TextureCubeArray.MostDetailedMip = 0;
    srv_desc.TextureCubeArray.MipLevels = mMipLevels;
    srv_desc.TextureCubeArray.First2DArrayFace = 0;
    srv_desc.TextureCubeArray.NumCubes = (UINT)count;

    hr = gDXDevice.getDevice()->CreateShaderResourceView(mTexture, &srv_desc, &mSRV);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXCubeArrayTexture::create: CreateShaderResourceView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mTexture->Release();
        mTexture = nullptr;
        return false;
    }

    return true;
}

bool DXCubeArrayTexture::copySliceFromBoundRenderTarget(int mip, int arraySlice, UINT src_width, UINT src_height)
{
    if (!mTexture || mip < 0 || (UINT)mip >= mMipLevels || arraySlice < 0 || (UINT)arraySlice >= mArraySize)
    {
        return false;
    }

    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv)
    {
        LL_WARNS("Texture") << "DXCubeArrayTexture::copySliceFromBoundRenderTarget: no render target currently bound" << LL_ENDL;
        return false;
    }

    ID3D11Resource* src_resource = nullptr;
    rtv->GetResource(&src_resource);
    rtv->Release();
    if (!src_resource)
    {
        return false;
    }

    UINT dst_subresource = D3D11CalcSubresource((UINT)mip, (UINT)arraySlice, mMipLevels);
    // S24 (2026-08-10, task #147/#184 follow-up): explicit D3D11_BOX when a
    // size is given - see this method's header comment for why the old
    // "nullptr box = whole subresource" assumption was wrong for the real
    // caller (a shrinking sub-region of a fixed-size scratch target, not a
    // dedicated per-mip target). No SRV of the destination array is created
    // or bound anywhere in this function - see this class's header comment
    // for why that matters.
    if (src_width > 0 && src_height > 0)
    {
        D3D11_BOX box = {};
        box.left = 0;
        box.top = 0;
        box.front = 0;
        box.right = src_width;
        box.bottom = src_height;
        box.back = 1;
        ctx->CopySubresourceRegion(mTexture, dst_subresource, 0, 0, 0, src_resource, 0, &box);
    }
    else
    {
        ctx->CopySubresourceRegion(mTexture, dst_subresource, 0, 0, 0, src_resource, 0, nullptr);
    }
    src_resource->Release();

    return true;
}

void DXCubeArrayTexture::generateMipMaps()
{
    if (mSRV && mGenerateMips)
    {
        gDXDevice.getContext()->GenerateMips(mSRV);
    }
}

void DXCubeArrayTexture::destroy()
{
    if (mSRV)
    {
        mSRV->Release();
        mSRV = nullptr;
    }
    if (mTexture)
    {
        mTexture->Release();
        mTexture = nullptr;
    }
    mMipLevels = 1;
    mArraySize = 0;
    mGenerateMips = false;
}
