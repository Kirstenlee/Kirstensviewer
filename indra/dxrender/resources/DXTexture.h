#pragma once
#include <d3d11.h>
#include <cstdint>
#include <vector>

// One already-encoded mip level's worth of block-compressed bytes, for
// DXTexture::createCompressedMips() below. `data` must stay valid for the
// duration of that call only (copied into the GPU resource synchronously,
// same convention as D3D11_SUBRESOURCE_DATA itself - no need to keep it
// alive afterward). `width`/`height` are this mip's REAL (possibly non-
// multiple-of-4) pixel dimensions, not a padded/block-aligned size - see
// createCompressedMips()'s own comment for why.
struct DXCompressedMipData
{
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
};

// Wraps one D3D11 2D texture + shader resource view, populated synchronously
// from a single top-level image. No discard-level streaming (see
// LLImageDX::setImage()'s DX_RENDER branch, the only caller) - that parity
// remains a documented follow-up gap. Real mip-chain generation IS supported
// via `generate_mips` for uncompressed formats, and block-compressed (BC1-3)
// upload IS supported via createCompressed() - mip0-only in that case, since
// D3D11 can't GenerateMips() a block-compressed resource.
//
// This class has no locking: nothing in this codebase calls into a DXTexture
// from any thread but the main one (the device itself is created with
// D3D11_CREATE_DEVICE_SINGLETHREADED, DXDevice.cpp). If threaded texture
// uploads are ever attempted again, they need a real main-thread-dispatch
// design, not a mutex added back to this class - getSRV() is on the hot
// path (called every texture bind, every draw call).
class DXTexture
{
public:
    ~DXTexture() { destroy(); }

    // `data` is `width * height * components` bytes, top-to-bottom row
    // order (matches LLImageRaw's layout). components is 1 (luminance),
    // 2 (luminance-alpha), 3 (RGB), or 4 (RGBA) - all are repacked to a
    // single RGBA8 GPU format: DXGI has no native 3-channel 8-bit format
    // (so RGB needs expanding regardless), and repacking 1/2-component
    // sources here lets them replicate across R/G/B the same way GL's
    // GL_LUMINANCE/GL_LUMINANCE_ALPHA sampling does, rather than leaving
    // G/B at 0.
    // `data` may be nullptr - mirrors GL's glTexImage2D(..., nullptr):
    // allocates GPU storage of the given size with undefined initial
    // content, no upload. LLViewerFetchedTexture's normal discard-level
    // streaming pattern creates the texture object before real pixel data
    // has arrived, then fills it in later via updateSubImage()/a follow-up
    // create() call - this is the common case, not a rare one. ALWAYS
    // destroys and reallocates, even when `data` is nullptr and an existing
    // texture already matches this size - do not add a same-size reuse
    // fast-path here; it breaks ordinary texture streaming under
    // RenderDXMultiThreadedTextures because the caller's own completion
    // tracking expects a real create() to happen every time it's called.
    // `generate_mips`: mirrors the source LLImageDX's mUseMipMaps - when
    // true, allocates the full auto mip chain
    // (D3D11_RESOURCE_MISC_GENERATE_MIPS) and, if `data` is non-null,
    // uploads it to mip 0 and calls GenerateMips() immediately.
    // `alpha_only`: only meaningful when components==1. GL's GL_ALPHA/
    // GL_ALPHA8 format (e.g. the terrain alpha_ramp gradient textures)
    // stores its single channel as the ALPHA component, RGB=0 - the
    // opposite of GL_LUMINANCE's "replicate into RGB, alpha=opaque"
    // convention. Callers must pass true when the source LLImageDX's
    // mFormatPrimary is GL_ALPHA, else the real data silently lands in
    // .rgb instead of .a. See repackPixel()'s comment in the .cpp.
    // `bgra`: true when `data`'s component order is actually BGRA, not
    // RGBA - CEF's native OnPaint buffer format. GL handles this natively
    // (the driver reorders in hardware); D3D11 has no equivalent
    // source-format parameter, so this class must swap R/B itself during
    // the repack, same as alpha_only's channel-remap.
    //
    // `raw_channels`: repackPixel()'s components==2 case assumes
    // "luminance-alpha" (replicate src[0] across RGB, src[1]->alpha) -
    // correct for actual grayscale+alpha source data, but wrong for a
    // genuine 2-independent-channel RG source (SMAA's AreaTex, sampled as
    // .rg by the shader). Set true to instead map dst.rg=src.rg directly,
    // no duplication.
    bool create(const uint8_t* data, int width, int height, int components, bool generate_mips = false, bool alpha_only = false, bool bgra = false, bool raw_channels = false);

    // Uploads a single mip-0 block-compressed
    // (BC1/BC2/BC3) image. `data` is already GPU-ready compressed bytes -
    // no repacking (unlike create(), which expands 1-4 component raw
    // pixels to RGBA8). `format` must be DXGI_FORMAT_BC1_UNORM,
    // DXGI_FORMAT_BC2_UNORM, or DXGI_FORMAT_BC3_UNORM (block sizes 8/16/16
    // bytes respectively - anything else is treated as 16). No
    // GenerateMips()/mip chain - D3D11 disallows
    // D3D11_RESOURCE_MISC_GENERATE_MIPS on block-compressed formats
    // (there's no way to render-target into a BC resource to compute
    // lower mips on the GPU), so this mirrors create()'s generate_mips=false
    // path: mip 0 only. `data` may be nullptr to allocate storage only,
    // same convention as create().
    bool createCompressed(const uint8_t* data, int width, int height, DXGI_FORMAT format);

    // Sibling to
    // createCompressed() above, extended for a FULL mip chain in one
    // CreateTexture2D call - kept as a separate method rather than an
    // overload since the data shape genuinely differs (a per-mip list, not
    // one buffer) and createCompressed()'s existing single-mip BC1-3
    // callers (pre-compressed S3TC asset sources, see its own comment)
    // don't need this. `mips[0]` is the top-level (most detailed) level;
    // `desc.Width`/`Height` use its REAL pixel size directly (matching
    // createCompressed()'s own established convention - D3D11 handles a
    // non-multiple-of-4 BC texture size internally, computing the same
    // ceil-to-4 block count this function and its caller both already use),
    // not any padded/block-aligned size. Building a full mip chain here
    // (rather than relying on GenerateMips(), impossible for block-
    // compressed formats - see this class's own header comment) is the
    // caller's job: each mip must already be independently BC-encoded from
    // its own raw pixel mip (see DXBC7Compressor::encodeMip() and
    // LLImageBase::generateMip(), the raw-mip-chain box filter this
    // pipeline reuses rather than inventing a new one).
    bool createCompressedMips(const std::vector<DXCompressedMipData>& mips, DXGI_FORMAT format);

    // Float-precision variant of create() above,
    // for procedural textures whose values genuinely exceed the [0,1] range
    // create()'s RGBA8 UNORM format can hold (e.g. pipeline.cpp's SSAO-style
    // noise map, whose Z component is a scale factor centered around 1.0,
    // not a normalized [-1,1] direction) - quantizing those to 8-bit would
    // silently clip/distort them. Always creates DXGI_FORMAT_R32G32B32A32_FLOAT
    // (simpler than packing to R16G16B16A16_FLOAT for the tiny, one-off
    // textures that need this - bandwidth isn't a concern at this size).
    // `data` is `width * height * components` floats, same row-order/
    // component-count convention as create() (1-4, repacked to 4).
    bool createFloat(const float* data, int width, int height, int components);

    void destroy();

    // Partial update of an already-created texture (e.g. adding one glyph to
    // a font atlas, or a discard-level refresh of a world texture). `data`
    // is the FULL source buffer, `data_width` its row stride in pixels (may
    // be wider than `width` - the sub-region being pushed is extracted from
    // within a larger buffer, mirroring GL's GL_UNPACK_ROW_LENGTH +
    // glTexSubImage2D usage in LLImageDX::setSubImage()). (x_pos, y_pos) is
    // the destination offset within this texture; (width, height) the
    // region size. `components` is repacked to RGBA8 the same way create()
    // does - see its comment. If this texture was created with
    // generate_mips=true, regenerates the mip chain from the updated mip 0
    // afterward. `alpha_only` - see create()'s comment, same meaning.
    // `bgra` - see create()'s comment, same meaning.
    bool updateSubImage(const uint8_t* data, int data_width, int x_pos, int y_pos, int width, int height, int components, bool alpha_only = false, bool bgra = false);

    // Streaming variant of create() for textures updated every frame from a
    // full source buffer (e.g. CEF media/web content - see
    // LLViewerMediaImpl::doMediaTexUpdate()). Allocates with
    // D3D11_USAGE_DYNAMIC + D3D11_CPU_ACCESS_WRITE instead of create()'s
    // DEFAULT usage, single mip only (D3D11 disallows combining DYNAMIC
    // usage with the RENDER_TARGET bind GenerateMips() needs - matches
    // media textures already being non-mipped). For 4-component,
    // non-alpha_only data this allocates DXGI_FORMAT_B8G8R8A8_UNORM (if
    // `bgra`) or DXGI_FORMAT_R8G8B8A8_UNORM (if not) directly from `data`
    // with no CPU repack - the GPU's own format swizzle handles the B/R
    // order, which is what lets updateDynamic() just memcpy whole rows
    // instead of looping a per-pixel repack every update (see its own
    // comment - this was the actual "Do Update" cost). Every other
    // component count/alpha_only combination still repacks to RGBA8 on the
    // CPU same as create(). `data`/`components`/`alpha_only`/`bgra`/
    // `raw_channels` - see create()'s comment, same meaning and repack
    // rules; `data` may be nullptr to allocate storage only, same
    // convention. Pair with updateDynamic() below, never updateSubImage() -
    // calling the DEFAULT-usage update path against a DYNAMIC resource is a
    // D3D11 validation failure.
    bool createDynamic(const uint8_t* data, int width, int height, int components, bool alpha_only = false, bool bgra = false, bool raw_channels = false);

    // Updates a createDynamic() texture via Map(D3D11_MAP_WRITE_DISCARD) -
    // the standard D3D11 pattern for a resource rewritten wholesale every
    // frame while still bound/sampled by in-flight rendering: DISCARD tells
    // the driver the old contents are irrelevant, so it hands back a fresh
    // backing allocation from its own internal pool instead of blocking the
    // CPU on the GPU's prior reads of this texture (which is exactly what
    // UpdateSubresource() against a DEFAULT-usage resource risks doing, if
    // called every frame against a texture also sampled every frame - see
    // LLViewerMediaImpl::doMediaTexUpdate()'s own comment history). Because
    // DISCARD invalidates the WHOLE resource, `data` must be the complete
    // (data_width x data_height) frame, not a sub-rect - callers that only
    // have a dirty sub-rect must have the full frame available some other
    // way (the media pipeline already does; see getBitsData()). Clamps the
    // write to this texture's actual (width, height) if `data`'s dimensions
    // disagree. `components`/`alpha_only`/`bgra` - see create()'s comment,
    // same meaning.
    bool updateDynamic(const uint8_t* data, int data_width, int data_height, int components, bool alpha_only = false, bool bgra = false);

    // Real D3D11 in-place downscale - the mechanism behind the
    // VRAM-pressure discard-bias system for already-resident textures.
    // `src_mip_level` is the ALREADY-COMPUTED mip level (this
    // texture was created with generate_mips=true, so its full mip chain
    // already exists on the GPU - no fresh downsample math needed, just a
    // resource copy) to promote to the new texture's mip 0, at
    // (new_width, new_height) - the real dimensions of that mip level.
    // Creates a new, smaller texture, copies that one mip level in via
    // CopySubresourceRegion (GPU-to-GPU, no CPU readback - see
    // DXCubeTexture::copyFace() for the same call shape), regenerates the
    // new texture's OWN remaining mip chain via GenerateMips(), then swaps
    // it in for the old resource. Returns false (no-op, old resource
    // untouched) if this texture has no mip chain (mGenerateMips false) -
    // every real caller (LLViewerLODTexture) is guaranteed to have one, see
    // the caller-side comment - or if any D3D11 call fails.
    bool scaleDown(int src_mip_level, int new_width, int new_height);

    // D3D11 equivalent of GL's
    // glCopyTexSubImage2D() - copies a (width, height) region from whatever
    // render target is *currently bound* (queried live via
    // OMGetRenderTargets(), not a caller-supplied handle - this matches
    // GL's own "operates on the current read framebuffer" behavior, and
    // works correctly whether that's a custom LLRenderTarget or the swap
    // chain's own back buffer, with no extra plumbing needed either way)
    // into this texture at (x_pos, y_pos). (fb_x, fb_y) use GL's
    // bottom-left-origin framebuffer convention - callers pass the exact
    // same coordinates they would to glCopyTexSubImage2D; the top-left/
    // bottom-left flip is handled internally (see DXReadback.h's comment
    // for the same recurring translation elsewhere in this codebase).
    // Returns false if nothing is bound, this texture has no resource yet,
    // or the copy itself fails.
    bool copySubImageFromFrameBuffer(int fb_x, int fb_y, int x_pos, int y_pos, int width, int height);

    ID3D11ShaderResourceView* getSRV() const
    {
        return mSRV;
    }

    // Real "does this hold a created GPU resource" check - used by
    // LLImageDX::getHasGLTexture()'s DX_RENDER branch instead of the GL-only
    // mTexName!=0 sentinel, which under DX_RENDER was only ever set to a
    // fake constant (1).
    bool isValid() const
    {
        return mTexture != nullptr;
    }

    // True when the CURRENT GPU resource is block-compressed (created via
    // createCompressed()/createCompressedMips()), false for the ordinary
    // RGBA8 path (create()/createFloat()). Any method that assumes RGBA8
    // stride math (updateSubImage(), scaleDown(), LLImageDX::readBackRaw()
    // via DXReadback::readPixels()) MUST check this first - reading/writing
    // RGBA8 stride against an actual BC7 (16-bytes-per-4x4-block) resource
    // is an out-of-bounds memcpy.
    bool isCompressedFormat() const
    {
        return mIsCompressed;
    }

    // Needed by LLImageDX::readBackRaw()'s DX_RENDER branch (GPU->CPU
    // readback via DXReadback, mirroring GL's glGetTexImage()) -
    // DXReadback::readPixels() takes a raw ID3D11Texture2D* source, not an
    // SRV.
    ID3D11Texture2D* getTexture() const
    {
        return mTexture;
    }

    // The same 1-4 component -> RGBA8 repack create() uses internally (see
    // its own comment), exposed as a small public utility so a caller
    // preparing data for something OTHER than this specific DXTexture's own
    // upload (the background BC7 compressor, which needs its OWN separate
    // RGBA8 copy of a texture's pixel data to encode off the main thread)
    // can reuse the exact same channel-remap rules rather than duplicating
    // them. `out` is resized to width*height*4 bytes. Returns false for an
    // unsupported component count (1-4 only), same as the internal version.
    static bool repackToRGBA8(const uint8_t* data, int width, int height, int components,
        std::vector<uint8_t>& out, bool alpha_only = false, bool bgra = false, bool raw_channels = false);

private:
    // Shared "release whatever GPU resources this instance currently holds"
    // body - create()/createCompressed()/createFloat()/destroy() all call
    // this as their first step. Name is a holdover from when this class had
    // locking (see class comment) but the split is still a useful shared
    // helper.
    void destroyLocked();

    ID3D11Texture2D* mTexture = nullptr;
    ID3D11ShaderResourceView* mSRV = nullptr;
    bool mGenerateMips = false;
    bool mIsCompressed = false;
    bool mIsDynamic = false;
};
