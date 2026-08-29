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
    // S24 (2026-08-06): `alpha_only` distinguishes GL_ALPHA-format 1-component
    // sources (e.g. the terrain alpha_ramp gradient, llviewertexturelist.cpp's
    // explicit GL_ALPHA8/GL_ALPHA override) from GL_LUMINANCE ones. GL's own
    // core-profile path for this (llimagegl.cpp setManualImage(), GL_ALPHA
    // branch) writes the single source byte into byte index 3 (alpha), RGB=0
    // - the opposite of GL_LUMINANCE's "replicate into RGB, alpha=opaque"
    // convention this function otherwise assumes for 1-component data.
    // Without this, alpha-only textures silently landed their real content in
    // .rgb (never sampled by any shader that reads them, since callers always
    // read .a for an "alpha ramp") while .a read back as a constant - found
    // chasing terrainF.hlsl's alpha_ramp always reading 0 blend weight despite
    // confirmed-correct, confirmed-varying UVs.
    // `bgra` (task #223/#222 follow-up, 2026-08-18): only meaningful for
    // components==3/4 - swaps the R/B read order so a BGR(A)-ordered source
    // (CEF's native OnPaint format) lands correctly in this always-RGBA8
    // destination. See DXTexture.h's create() comment for the full story.
    bool repackPixel(const uint8_t* src, int components, uint8_t* dst, bool alpha_only = false, bool bgra = false)
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
        case 2: // luminance-alpha
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = src[1];
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

bool DXTexture::create(const uint8_t* data, int width, int height, int components, bool generate_mips, bool alpha_only, bool bgra)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    // S24 (2026-08-24, task #257): a same-size/no-data reuse fast-path was
    // tried here (skip destroy+recreate, keep old content, for CEF's
    // every-paint create() call) and reverted - data==nullptr at a matching
    // size is NOT CEF-specific, it's LLViewerFetchedTexture's normal
    // discard-level streaming pattern too (see this class's create()
    // comment in the header). Silently keeping stale GPU content there
    // broke ordinary texture loading under RenderDXMultiThreadedTextures
    // (wrong/stale textures, retry storms, unbounded memory growth,
    // avatars never rezzing) - the caller's own completion tracking expects
    // a real create() to have actually happened. If CEF's blank-frame gap
    // needs closing again, it belongs at the LLViewerMediaImpl layer, which
    // actually knows "this is a media texture, safe to reuse in place" -
    // not inside this shared, every-texture-in-the-engine create().

    // S24 (2026-07-23): a per-instance call-counter diagnostic here
    // confirmed create() was being invoked repeatedly (destroy()+recreate,
    // wiping any previously uploaded content and handing out a fresh SRV)
    // on the SAME font-atlas DXTexture - traced to LLImageGL::setSubImage()'s
    // full-image HACK branch and fixed there (llimagegl.cpp). This function
    // is meant to run once per texture's lifetime plus whenever its
    // dimensions/format genuinely change; incremental content updates
    // belong in updateSubImage() below, not here.
    destroyLocked();

    mGenerateMips = generate_mips;

    const uint8_t* upload_data = data;
    std::vector<uint8_t> rgba;

    // S24 (task #223/#222 follow-up): components==4 used to always take the
    // zero-copy fast path (upload_data = data directly) since RGBA8 source
    // data needs no repacking. That's no longer true when bgra is set - a
    // BGRA source still needs its R/B channels swapped even though the
    // component count already matches the destination, so it must go
    // through repackPixel() too, not just non-4-component sources.
    if (data && (components != 4 || bgra))
    {
        rgba.resize((size_t)width * height * 4);
        for (int i = 0; i < width * height; ++i)
        {
            if (!repackPixel(data + (size_t)i * components, components, &rgba[(size_t)i * 4], alpha_only, bgra))
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

    // S24 (2026-07-23, stage 7): every DX_RENDER texture previously got
    // exactly 1 mip level (MipLevels=1) no matter what the source
    // LLImageGL's mUseMipMaps said - a real, silent quality gap for every
    // world/avatar texture found auditing llviewertexture.cpp's texture-
    // creation path (aliasing on minified textures; trilinear/anisotropic
    // sampler filtering has nothing to blend between). GenerateMips()
    // requires BOTH D3D11_BIND_RENDER_TARGET (alongside the usual
    // SHADER_RESOURCE) and the RESOURCE_MISC_GENERATE_MIPS flag, and
    // MipLevels=0 to request the full auto chain down to 1x1. D3D11 also
    // requires no initial data at creation time when generating mips this
    // way (there's no single pInitialData shape that means "one real mip
    // plus N generated ones") - so mip 0 gets uploaded via
    // UpdateSubresource() immediately after creation instead, then
    // GenerateMips() fills in the rest.
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

    // S24 (2026-07-23): a readback diagnostic here confirmed CreateTexture2D's
    // initial-data upload genuinely lands on the GPU intact - the "invisible
    // menu text" bug was elsewhere (LLFontVertexBuffer's uncached-replay gap,
    // see llfontvertexbuffer.cpp). Ruled out, not revisiting.

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

bool DXTexture::createFloat(const float* data, int width, int height, int components)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    destroyLocked();
    mGenerateMips = false;

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

    // S24 (2026-07-23): a readback diagnostic here confirmed UpdateSubresource()
    // genuinely lands the correct per-glyph bytes on the GPU - the "invisible
    // menu text" bug was elsewhere (LLFontVertexBuffer's uncached-replay gap,
    // see llfontvertexbuffer.cpp). Ruled out, not revisiting.

    // S24 (2026-07-23, stage 7): if this texture was created with
    // generate_mips=true, mip 0 just changed - regenerate the rest of the
    // chain from it so lower mips don't go stale (e.g. a world texture's
    // discard-level refresh, or any future non-font caller that both wants
    // mips and updates content incrementally). Font glyph atlases (the
    // main updateSubImage() caller today) never request mips, so this is a
    // no-op for them.
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

    // S24 (2026-08-16): src_mip_level/new_width/new_height are all computed
    // by the caller (LLImageGL::scaleDown()) from GL-style discard-level
    // arithmetic (getWidth(discard)/getHeight(discard)), which is NOT
    // guaranteed to exactly match what D3D11's own automatic mip chain
    // (MipLevels=0 at create() time) actually generated for this texture's
    // REAL dimensions - confirmed via a live crash: CopySubresourceRegion's
    // debug-layer PreValidation rejected the copy outright (a clean
    // exception, not silent corruption - D3D11 requires the source
    // subresource to fit within the destination subresource, and the
    // caller's guessed destination size didn't match the source's real
    // size). Rather than trust the caller's guess for the destination's
    // creation size, derive it from mTexture's own real, GPU-verified
    // dimensions - this guarantees the copy always fits, regardless of any
    // discrepancy between LLImageGL's discard-level math and D3D11's actual
    // mip chain (root cause not fully pinned down - possibly non-power-of-
    // two source textures, e.g. avatar impostors/bakes, whose dimensions
    // don't halve identically under bit-shift vs iterative floor-halving -
    // but this fix closes the whole class regardless of the exact trigger).
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

    // S24 (2026-08-09, task #157/#166): this function trusts "whatever
    // render target OMGetRenderTargets() currently reports" to be the
    // right size for the requested copy, with zero validation - if it
    // isn't (e.g. a caller expecting a large bake/preview target but
    // something else is actually bound), dx_fb_y goes negative and the
    // UINT cast wraps to a huge value (confirmed via a real D3D11 debug-
    // layer capture: "pSrcBox... top:4294965760" = -1536 as signed,
    // requesting a 2048x2048 copy against a far smaller bound target).
    // CopySubresourceRegion with an invalid box doesn't just fail
    // gracefully - it took down the entire D3D11 device (DXGI_ERROR_
    // DEVICE_REMOVED, every subsequent call failing) rather than
    // rejecting the one bad call, hanging the client. Validating and
    // rejecting here converts that into a safe, logged no-op, matching
    // the "safe no-op over silent/catastrophic failure" pattern already
    // established everywhere else in this port. Doesn't fix WHY the
    // wrong target ended up bound in the first place - that's task
    // #166's open question - but stops it from being able to crash the
    // GPU while that's investigated.
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
