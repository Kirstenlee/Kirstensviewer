#include "DXTexture.h"
#include "DXDevice.h"
#include "llerror.h"
#include <vector>
#include <algorithm>

namespace
{
    // Repacks one pixel (components bytes at `src`) into 4 RGBA8 bytes at
    // `dst` - shared by create() and updateSubImage() so the format-repack
    // rules (see DXTexture.h's comment) are defined exactly once. Returns
    // false for an unsupported component count (caller logs/fails).
    //
    // `alpha_only` distinguishes GL_ALPHA-format 1-component sources (e.g.
    // the terrain alpha_ramp gradient) from GL_LUMINANCE ones: it writes the
    // single source byte into byte index 3 (alpha), RGB=0 - the opposite of
    // GL_LUMINANCE's "replicate into RGB, alpha=opaque" convention this
    // function otherwise assumes for 1-component data.
    // `bgra` (only meaningful for components==3/4) swaps the R/B read order
    // so a BGR(A)-ordered source (CEF's native OnPaint format) lands
    // correctly in this always-RGBA8 destination. See DXTexture.h's
    // create() comment.
    // `raw_channels` (only meaningful for components==2) - see DXTexture.h's
    // create() comment. False (default) keeps the original luminance-alpha
    // interpretation for real grayscale+alpha sources.
    bool repackPixel(const uint8_t* src, int components, uint8_t* dst, bool alpha_only = false, bool bgra = false, bool raw_channels = false)
    {
        if (components == 1 && alpha_only)
        {
            dst[0] = dst[1] = dst[2] = 0;
            dst[3] = src[0];
            return true;
        }

        switch (components)
        {
        case 1: // luminance - replicate across RGB, opaque
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = 255;
            return true;
        case 2:
            if (raw_channels)
            { // two genuinely independent channels (e.g. SMAA's AreaTex) - preserve both in R/G, don't duplicate
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = 0;
                dst[3] = 255;
            }
            else
            { // luminance-alpha
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = src[1];
            }
            return true;
        case 3: // RGB / BGR
            dst[0] = bgra ? src[2] : src[0];
            dst[1] = src[1];
            dst[2] = bgra ? src[0] : src[2];
            dst[3] = 255;
            return true;
        case 4: // RGBA / BGRA
            dst[0] = bgra ? src[2] : src[0];
            dst[1] = src[1];
            dst[2] = bgra ? src[0] : src[2];
            dst[3] = src[3];
            return true;
        default:
            return false;
        }
    }
}

bool DXTexture::repackToRGBA8(const uint8_t* data, int width, int height, int components,
    std::vector<uint8_t>& out, bool alpha_only, bool bgra, bool raw_channels)
{
    if (width <= 0 || height <= 0 || !data)
    {
        return false;
    }

    out.resize((size_t)width * height * 4);
    for (int i = 0; i < width * height; ++i)
    {
        if (!repackPixel(data + (size_t)i * components, components, out.data() + (size_t)i * 4, alpha_only, bgra, raw_channels))
        {
            return false;
        }
    }
    return true;
}

bool DXTexture::create(const uint8_t* data, int width, int height, int components, bool generate_mips, bool alpha_only, bool bgra, bool raw_channels)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    // Do NOT add a same-size/no-data reuse fast-path here (skip
    // destroy+recreate, keep old content) - data==nullptr at a matching size
    // is LLViewerFetchedTexture's normal discard-level streaming pattern,
    // not just CEF's every-paint call, and silently keeping stale GPU
    // content breaks ordinary texture loading under
    // RenderDXMultiThreadedTextures (the caller's completion tracking
    // expects a real create() to have happened). A CEF-specific reuse
    // optimization belongs at the LLViewerMediaImpl layer instead.
    //
    // This function is meant to run once per texture's lifetime plus
    // whenever its dimensions/format genuinely change; incremental content
    // updates belong in updateSubImage() below, not here.
    destroyLocked();

    mGenerateMips = generate_mips;
    mIsCompressed = false;

    const uint8_t* upload_data = data;
    std::vector<uint8_t> rgba;

    // components==4 alone would allow the zero-copy fast path (upload_data =
    // data directly), but a BGRA source still needs its R/B channels swapped
    // even though the component count already matches the destination, so
    // bgra must also route through repackPixel().
    if (data && (components != 4 || bgra))
    {
        rgba.resize((size_t)width * height * 4);
        for (int i = 0; i < width * height; ++i)
        {
            if (!repackPixel(data + (size_t)i * components, components, &rgba[(size_t)i * 4], alpha_only, bgra, raw_channels))
            {
                LL_WARNS("Texture") << "DXTexture::create: unsupported component count " << components << LL_ENDL;
                return false;
            }
        }
        upload_data = rgba.data();
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;

    // GenerateMips() requires BOTH D3D11_BIND_RENDER_TARGET (alongside the
    // usual SHADER_RESOURCE) and the RESOURCE_MISC_GENERATE_MIPS flag, and
    // MipLevels=0 to request the full auto chain down to 1x1. D3D11 also
    // requires no initial data at creation time when generating mips this
    // way - so mip 0 gets uploaded via UpdateSubresource() immediately after
    // creation instead, then GenerateMips() fills in the rest.
    if (generate_mips)
    {
        desc.MipLevels = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }
    else
    {
        desc.MipLevels = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    }

    // No initial data (nullptr) - mirrors glTexImage2D(..., nullptr):
    // allocate storage only, undefined content, filled in later via
    // updateSubImage(). CreateTexture2D requires a null pInitialData
    // pointer in this case, not a D3D11_SUBRESOURCE_DATA with a null
    // pSysMem. Also always nullptr when generate_mips is requested (see
    // comment above) - mip 0 is uploaded separately, below.
    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = upload_data;
    init_data.SysMemPitch = width * 4;

    const bool use_init_data = (upload_data != nullptr) && !generate_mips;

    HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&desc, use_init_data ? &init_data : nullptr, &mTexture);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "CreateTexture2D failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    hr = gDXDevice.getDevice()->CreateShaderResourceView(mTexture, nullptr, &mSRV);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "CreateShaderResourceView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mTexture->Release();
        mTexture = nullptr;
        return false;
    }

    if (generate_mips && upload_data)
    {
        gDXDevice.getContext()->UpdateSubresource(mTexture, 0, nullptr, upload_data, width * 4, 0);
        gDXDevice.getContext()->GenerateMips(mSRV);
    }

    return true;
}

bool DXTexture::createCompressed(const uint8_t* data, int width, int height, DXGI_FORMAT format)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    destroyLocked();
    mGenerateMips = false;
    mIsCompressed = true;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.ArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    // BC1 packs a 4x4 block into 8 bytes; BC2/BC3 use 16 bytes/block.
    // Partial edge blocks (width/height not a multiple of 4) still occupy
    // one full block, hence the ceil-to-4 rounding - matches
    // LLImageGL::dataFormatBytes()'s own "if (width < 4) width = 4"-style
    // block-count rounding for the same GL compressed formats.
    const UINT block_size = (format == DXGI_FORMAT_BC1_UNORM) ? 8 : 16;
    const UINT blocks_wide = (UINT)((width + 3) / 4);
    const UINT blocks_high = (UINT)((height + 3) / 4);

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = data;
    init_data.SysMemPitch = blocks_wide * block_size;
    init_data.SysMemSlicePitch = init_data.SysMemPitch * blocks_high;

    HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&desc, data ? &init_data : nullptr, &mTexture);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXTexture::createCompressed: CreateTexture2D failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    hr = gDXDevice.getDevice()->CreateShaderResourceView(mTexture, nullptr, &mSRV);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXTexture::createCompressed: CreateShaderResourceView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mTexture->Release();
        mTexture = nullptr;
        return false;
    }

    return true;
}

bool DXTexture::createCompressedMips(const std::vector<DXCompressedMipData>& mips, DXGI_FORMAT format)
{
    if (mips.empty() || mips[0].width <= 0 || mips[0].height <= 0 || !mips[0].data)
    {
        return false;
    }

    destroyLocked();
    mGenerateMips = false;
    mIsCompressed = true;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = mips[0].width;
    desc.Height = mips[0].height;
    desc.ArraySize = 1;
    desc.MipLevels = (UINT)mips.size();
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    // BC7 (the only format this method is used for today) packs a 4x4 block
    // into 16 bytes - same block-size convention as createCompressed()
    // above, computed per-mip here since each mip has its own dimensions.
    const UINT block_size = (format == DXGI_FORMAT_BC1_UNORM) ? 8 : 16;

    std::vector<D3D11_SUBRESOURCE_DATA> init_data(mips.size());
    for (size_t i = 0; i < mips.size(); ++i)
    {
        if (!mips[i].data || mips[i].width <= 0 || mips[i].height <= 0)
        {
            return false;
        }
        const UINT blocks_wide = (UINT)((mips[i].width + 3) / 4);
        const UINT blocks_high = (UINT)((mips[i].height + 3) / 4);
        init_data[i].pSysMem = mips[i].data;
        init_data[i].SysMemPitch = blocks_wide * block_size;
        init_data[i].SysMemSlicePitch = init_data[i].SysMemPitch * blocks_high;
    }

    HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&desc, init_data.data(), &mTexture);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXTexture::createCompressedMips: CreateTexture2D failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    hr = gDXDevice.getDevice()->CreateShaderResourceView(mTexture, nullptr, &mSRV);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXTexture::createCompressedMips: CreateShaderResourceView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mTexture->Release();
        mTexture = nullptr;
        return false;
    }

    return true;
}

bool DXTexture::createFloat(const float* data, int width, int height, int components)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    destroyLocked();
    mGenerateMips = false;
    mIsCompressed = false;

    std::vector<float> rgba;
    const float* upload_data = data;

    if (data && components != 4)
    {
        rgba.resize((size_t)width * height * 4, 0.0f);
        for (int i = 0; i < width * height; ++i)
        {
            const float* src = data + (size_t)i * components;
            float* dst = &rgba[(size_t)i * 4];
            switch (components)
            {
            case 1:
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = 1.0f;
                break;
            case 2:
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = src[1];
                break;
            case 3:
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 1.0f;
                break;
            default:
                LL_WARNS("Texture") << "DXTexture::createFloat: unsupported component count " << components << LL_ENDL;
                return false;
            }
        }
        upload_data = rgba.data();
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.ArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = upload_data;
    init_data.SysMemPitch = width * 4 * sizeof(float);

    HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&desc, upload_data ? &init_data : nullptr, &mTexture);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "createFloat: CreateTexture2D failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    hr = gDXDevice.getDevice()->CreateShaderResourceView(mTexture, nullptr, &mSRV);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "createFloat: CreateShaderResourceView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mTexture->Release();
        mTexture = nullptr;
        return false;
    }

    return true;
}

bool DXTexture::updateSubImage(const uint8_t* data, int data_width, int x_pos, int y_pos, int width, int height, int components, bool alpha_only, bool bgra)
{
    if (!mTexture || !data || width <= 0 || height <= 0)
    {
        return false;
    }

    // See isCompressedFormat()'s header comment - this method's RGBA8-repack
    // UpdateSubresource() below is only valid for the uncompressed path.
    if (mIsCompressed)
    {
        return false;
    }

    // Extract the (x_pos, y_pos, width, height) rectangle out of the larger
    // data_width-strided source buffer into a tightly-packed RGBA8 buffer -
    // mirrors GL's GL_UNPACK_ROW_LENGTH + glTexSubImage2D combination in
    // LLImageGL::setSubImage().
    std::vector<uint8_t> rgba((size_t)width * height * 4);
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* src_row = data + ((size_t)(y_pos + y) * data_width + x_pos) * components;
        uint8_t* dst_row = &rgba[(size_t)y * width * 4];
        for (int x = 0; x < width; ++x)
        {
            if (!repackPixel(src_row + (size_t)x * components, components, dst_row + (size_t)x * 4, alpha_only, bgra))
            {
                LL_WARNS("Texture") << "DXTexture::updateSubImage: unsupported component count " << components << LL_ENDL;
                return false;
            }
        }
    }

    D3D11_BOX box = {};
    box.left = (UINT)x_pos;
    box.top = (UINT)y_pos;
    box.front = 0;
    box.right = (UINT)(x_pos + width);
    box.bottom = (UINT)(y_pos + height);
    box.back = 1;

    gDXDevice.getContext()->UpdateSubresource(mTexture, 0, &box, rgba.data(), width * 4, 0);

    // If this texture was created with generate_mips=true, mip 0 just
    // changed - regenerate the rest of the chain from it so lower mips
    // don't go stale. Font glyph atlases (the main updateSubImage() caller
    // today) never request mips, so this is a no-op for them.
    if (mGenerateMips)
    {
        gDXDevice.getContext()->GenerateMips(mSRV);
    }

    return true;
}

bool DXTexture::scaleDown(int src_mip_level, int new_width, int new_height)
{
    if (!mTexture || !mGenerateMips || new_width <= 0 || new_height <= 0 || src_mip_level < 0)
    {
        return false;
    }

    // src_mip_level/new_width/new_height are computed by the caller
    // (LLImageGL::scaleDown()) from GL-style discard-level arithmetic, which
    // is NOT guaranteed to exactly match what D3D11's own automatic mip
    // chain (MipLevels=0 at create() time) actually generated for this
    // texture's real dimensions - D3D11 requires the source subresource to
    // fit within the destination, so a size mismatch here makes
    // CopySubresourceRegion's debug-layer PreValidation reject the copy
    // outright. Derive the destination's creation size from mTexture's own
    // real, GPU-verified dimensions instead of trusting the caller's guess,
    // so the copy always fits.
    D3D11_TEXTURE2D_DESC src_desc = {};
    mTexture->GetDesc(&src_desc);
    if ((UINT)src_mip_level >= src_desc.MipLevels)
    {
        LL_WARNS("Texture") << "DXTexture::scaleDown: src_mip_level " << src_mip_level
            << " out of range (texture has " << src_desc.MipLevels << " mip levels)" << LL_ENDL;
        return false;
    }

    const int real_width = (int)std::max(1u, src_desc.Width >> src_mip_level);
    const int real_height = (int)std::max(1u, src_desc.Height >> src_mip_level);
    if (real_width != new_width || real_height != new_height)
    {
        LL_WARNS_ONCE("Texture") << "DXTexture::scaleDown: caller-computed size ("
            << new_width << "x" << new_height << ") disagrees with mip " << src_mip_level
            << "'s real size (" << real_width << "x" << real_height
            << ") - using the real size. Source texture is " << src_desc.Width << "x" << src_desc.Height
            << " with " << src_desc.MipLevels << " mip levels." << LL_ENDL;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = real_width;
    desc.Height = real_height;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    // Same generate_mips desc shape as create() - the new, smaller texture
    // needs its own full mip chain too (further discard-level increases
    // later reuse this same scaleDown() path against ITS mips).
    desc.MipLevels = 0;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ID3D11Texture2D* new_texture = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateTexture2D(&desc, nullptr, &new_texture);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXTexture::scaleDown: CreateTexture2D failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return false;
    }

    ID3D11ShaderResourceView* new_srv = nullptr;
    hr = gDXDevice.getDevice()->CreateShaderResourceView(new_texture, nullptr, &new_srv);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "DXTexture::scaleDown: CreateShaderResourceView failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        new_texture->Release();
        return false;
    }

    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    // GPU-to-GPU copy of an ALREADY-COMPUTED mip level - no fresh downsample
    // math needed, GenerateMips() already did that work back when this
    // texture (mTexture, the OLD one) was first created/uploaded. Source box
    // left null (= copy the whole source subresource) - safe now that
    // new_texture was created at real_width/real_height, derived from
    // mTexture's own real dimensions above, so source and destination are
    // guaranteed to agree. Precedent for this call shape:
    // DXCubeTexture::copyFace().
    ctx->CopySubresourceRegion(new_texture, 0, 0, 0, 0, mTexture, (UINT)src_mip_level, nullptr);
    ctx->GenerateMips(new_srv);

    // Swap in the new, smaller resource.
    mTexture->Release();
    mSRV->Release();
    mTexture = new_texture;
    mSRV = new_srv;

    return true;
}

bool DXTexture::copySubImageFromFrameBuffer(int fb_x, int fb_y, int x_pos, int y_pos, int width, int height)
{
    if (!mTexture || width <= 0 || height <= 0)
    {
        return false;
    }

    // See isCompressedFormat()'s header comment - CopySubresourceRegion()
    // below requires format-compatible source/destination, which an
    // ordinary (RGBA8) render target and a BC7 destination are not.
    if (mIsCompressed)
    {
        return false;
    }

    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv)
    {
        LL_WARNS("Texture") << "DXTexture::copySubImageFromFrameBuffer: no render target currently bound" << LL_ENDL;
        return false;
    }

    ID3D11Resource* src_resource = nullptr;
    rtv->GetResource(&src_resource);
    rtv->Release();
    if (!src_resource)
    {
        return false;
    }

    // Need the source's height to translate GL's bottom-left-origin fb_y
    // into D3D11's top-left-origin row order - same "source_height - height
    // - gl_y_offset" translation already established for DXReadback's
    // callers (see llviewerwindow.cpp).
    ID3D11Texture2D* src_tex = nullptr;
    src_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src_tex);
    src_resource->Release();
    if (!src_tex)
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC src_desc = {};
    src_tex->GetDesc(&src_desc);

    const int dx_fb_y = (int)src_desc.Height - fb_y - height;

    // If the currently-bound render target isn't the size the caller
    // expects, dx_fb_y can go negative and the UINT cast wraps to a huge
    // value. CopySubresourceRegion with an invalid box doesn't fail
    // gracefully - it takes down the entire D3D11 device (DXGI_ERROR_
    // DEVICE_REMOVED, every subsequent call failing). Validate and reject
    // here instead, converting that into a safe, logged no-op.
    if (dx_fb_y < 0 || fb_x < 0 ||
        (UINT)(fb_x + width) > src_desc.Width ||
        (UINT)(dx_fb_y + height) > src_desc.Height)
    {
        LL_WARNS("Texture") << "DXTexture::copySubImageFromFrameBuffer: requested region ("
            << fb_x << "," << dx_fb_y << ")-(" << (fb_x + width) << "," << (dx_fb_y + height)
            << ") does not fit the currently bound render target (" << src_desc.Width << "x" << src_desc.Height
            << ") - rejecting copy instead of issuing an invalid CopySubresourceRegion." << LL_ENDL;
        src_tex->Release();
        return false;
    }

    D3D11_BOX box = {};
    box.left = (UINT)fb_x;
    box.top = (UINT)dx_fb_y;
    box.front = 0;
    box.right = (UINT)(fb_x + width);
    box.bottom = (UINT)(dx_fb_y + height);
    box.back = 1;

    ctx->CopySubresourceRegion(mTexture, 0, (UINT)x_pos, (UINT)y_pos, 0, src_tex, 0, &box);
    src_tex->Release();

    // Same mip-regen-after-mip0-write rule as updateSubImage() above - real callers include
    // llterrainpaintmap.cpp's PBR paintmap bake, whose destination texture is mip-chained. The GL
    // path's caller used to follow this copy with its own glGenerateMipmap(GL_TEXTURE_2D) call,
    // which is unresolved/null under DX_RENDER (glGenerateMipmap needs a real GL 3.0+ context to
    // load via wglGetProcAddress, never created here) - calling through it crashed outright.
    if (mGenerateMips)
    {
        ctx->GenerateMips(mSRV);
    }

    return true;
}

void DXTexture::destroy()
{
    destroyLocked();
}

void DXTexture::destroyLocked()
{
    if (mSRV) { mSRV->Release(); mSRV = nullptr; }
    if (mTexture) { mTexture->Release(); mTexture = nullptr; }
}
