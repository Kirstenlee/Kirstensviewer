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
    // D3D11 has no native 3-channel float format (GL's hdr path uses
    // GL_R11F_G11F_B10F). R16G16B16A16_FLOAT is used rather than
    // R11G11B10_FLOAT because its D3D11_FORMAT_SUPPORT_MIP_AUTOGEN is
    // guaranteed at feature-level 11 baseline while R11G11B10_FLOAT's isn't,
    // and this array is always created with generate_mips=true in practice.
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
    mWidth = actual_desc.Width;
    mHeight = actual_desc.Height;
    mFormat = actual_desc.Format;

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

    // CopySubresourceRegion() returns no HRESULT and fails silently on a bad copy, so the
    // preconditions it relies on are checked here before anything is issued.
    ID3D11Texture2D* src_tex = nullptr;
    HRESULT hr = src_resource->QueryInterface(IID_PPV_ARGS(&src_tex));
    if (FAILED(hr) || !src_tex)
    {
        LL_WARNS_ONCE("Texture") << "DXCubeArrayTexture::copySliceFromBoundRenderTarget: bound target is not a 2D texture" << LL_ENDL;
        src_resource->Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC src_desc = {};
    src_tex->GetDesc(&src_desc);
    src_tex->Release();

    // The caller's region, or the whole bound target when no size is given.
    UINT copy_w = src_width > 0 ? src_width : src_desc.Width;
    UINT copy_h = src_height > 0 ? src_height : src_desc.Height;

    UINT dst_w = mWidth >> mip;
    UINT dst_h = mHeight >> mip;
    if (dst_w == 0) dst_w = 1;
    if (dst_h == 0) dst_h = 1;

    if (src_desc.Format != mFormat || src_desc.SampleDesc.Count != 1 ||
        copy_w > src_desc.Width || copy_h > src_desc.Height ||
        copy_w > dst_w || copy_h > dst_h)
    {
        LL_WARNS_ONCE("Texture") << "DXCubeArrayTexture::copySliceFromBoundRenderTarget: copy rejected, mip " << mip
            << " slice " << arraySlice << " copy " << copy_w << "x" << copy_h
            << " src " << src_desc.Width << "x" << src_desc.Height << " fmt " << src_desc.Format << " samples " << src_desc.SampleDesc.Count
            << " dst " << dst_w << "x" << dst_h << " fmt " << mFormat << LL_ENDL;
        src_resource->Release();
        return false;
    }

    UINT dst_subresource = D3D11CalcSubresource((UINT)mip, (UINT)arraySlice, mMipLevels);
    // Explicit D3D11_BOX when a size is given - see this method's header
    // comment for why "nullptr box = whole subresource" isn't always right.
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
