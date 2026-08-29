#include "DXCubeTexture.h"
#include "DXDevice.h"
#include "llerror.h"

bool DXCubeTexture::create(int width, int height, bool generate_mips)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    destroy();
    mGenerateMips = generate_mips;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    // Same two-step generate_mips pattern as DXTexture::create(): a mip
    // chain requires D3D11_BIND_RENDER_TARGET + GENERATE_MIPS at creation
    // time, and MipLevels=0 requests the full auto chain down to 1x1 - the
    // real level count is queried back below via GetDesc(), needed to
    // compute correct subresource indices in copyFace().
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
        LL_WARNS("Texture") << "DXCubeTexture::create: CreateTexture2D failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    D3D11_TEXTURE2D_DESC actual_desc = {};
    mTexture->GetDesc(&actual_desc);
    mMipLevels = actual_desc.MipLevels;

    hr = gDXDevice.getDevice()->CreateShaderResourceView(mTexture, nullptr, &mSRV);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXCubeTexture::create: CreateShaderResourceView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mTexture->Release();
        mTexture = nullptr;
        return false;
    }

    return true;
}

bool DXCubeTexture::copyFace(int face, ID3D11Texture2D* face_texture)
{
    if (!mTexture || !face_texture || face < 0 || face >= 6)
    {
        return false;
    }

    UINT dst_subresource = D3D11CalcSubresource(0, (UINT)face, mMipLevels);
    gDXDevice.getContext()->CopySubresourceRegion(mTexture, dst_subresource, 0, 0, 0, face_texture, 0, nullptr);
    return true;
}

void DXCubeTexture::generateMipMaps()
{
    if (mSRV && mGenerateMips)
    {
        gDXDevice.getContext()->GenerateMips(mSRV);
    }
}

void DXCubeTexture::destroy()
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
    mGenerateMips = false;
}
