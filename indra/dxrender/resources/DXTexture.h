#pragma once
#include <d3d11.h>
#include <cstdint>
#include <vector>

// Wraps one D3D11 2D texture + shader resource view, populated synchronously
// from a single top-level image. No discard-level streaming (see
// LLImageGL::setImage()'s DX_RENDER branch, the only caller) - that parity
// remains a documented follow-up gap. Real mip-chain generation IS supported
// (stage 7, 2026-07-23) via `generate_mips` for uncompressed formats, and
// block-compressed (BC1-3) upload IS supported (task #85, 2026-08-16) via
// createCompressed() - mip0-only in that case, since D3D11 can't
// GenerateMips() a block-compressed resource.
//
// S24 (2026-08-16, task #98-adjacent, KVTweaks "DX Texture/Media Loading"
// overhaul): create()/updateSubImage() can optionally defer their GPU upload
// via `defer_upload`. D3D11's threading model makes this safe: ID3D11Device
// methods (CreateTexture2D/CreateShaderResourceView - everything up to the
// deferred point) are free-threaded per the D3D11 spec, so the repack +
// texture/SRV creation genuinely can run on a background thread concurrently
// with rendering. Only ID3D11DeviceContext methods (UpdateSubresource/
// GenerateMips) are NOT thread-safe - those are what `defer_upload` holds
// back, to be finished later via finalizePendingUpload() on the main thread.
// Callers that don't pass defer_upload see zero behavior change.
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
    // create() call - this is the common case, not a rare one.
    // `generate_mips` (new, stage 7): mirrors the source LLImageGL's
    // mUseMipMaps - when true, allocates the full auto mip chain
    // (D3D11_RESOURCE_MISC_GENERATE_MIPS) and, if `data` is non-null,
    // uploads it to mip 0 and calls GenerateMips() immediately. Without
    // this, every DX_RENDER texture previously had exactly 1 mip level
    // regardless of what the caller asked for - a real, silent quality gap
    // (no mip = no trilinear/anisotropic benefit, aliasing on minified
    // textures) found auditing llviewertexture.cpp's texture-creation path.
    // `alpha_only` (2026-08-06): only meaningful when components==1. GL's
    // GL_ALPHA/GL_ALPHA8 format (e.g. the terrain alpha_ramp gradient
    // textures) stores its single channel as the ALPHA component, RGB=0 -
    // the opposite of GL_LUMINANCE's "replicate into RGB, alpha=opaque"
    // convention. Callers must pass true when the source LLImageGL's
    // mFormatPrimary is GL_ALPHA, else the real data silently lands in
    // .rgb instead of .a. See repackPixel()'s comment in the .cpp.
    // `defer_upload` - see this class's top comment. When true and a mip-0
    // upload would otherwise happen (generate_mips && data != nullptr), the
    // repacked bytes are stashed instead of uploaded - call
    // finalizePendingUpload() on the main thread afterward to complete it.
    // `bgra` (task #223/#222 follow-up, 2026-08-18): true when `data`'s
    // component order is actually BGRA, not RGBA - CEF's native OnPaint
    // buffer format (media_plugin_cef.cpp declares GL_BGRA via its
    // texture_params message, LLImageGL::mFormatPrimary carries it through).
    // GL handles this natively (glTexImage2D(..., GL_BGRA, ...) - the driver
    // reorders in hardware); D3D11 has no equivalent source-format
    // parameter, so this class must swap R/B itself during the repack, same
    // as alpha_only's channel-remap. Without this, CEF content (web media,
    // the login screen) uploads with R and B swapped - a systematic hue
    // shift, not a corruption - every pixel, every frame.
    bool create(const uint8_t* data, int width, int height, int components, bool generate_mips = false, bool alpha_only = false, bool defer_upload = false, bool bgra = false);

    // S24 (2026-08-16, task #85): uploads a single mip-0 block-compressed
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

    // S24 (2026-08-03, task #84): float-precision variant of create() above,
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
    // glTexSubImage2D usage in LLImageGL::setSubImage()). (x_pos, y_pos) is
    // the destination offset within this texture; (width, height) the
    // region size. `components` is repacked to RGBA8 the same way create()
    // does - see its comment. If this texture was created with
    // generate_mips=true, regenerates the mip chain from the updated mip 0
    // afterward. `alpha_only` - see create()'s comment, same meaning.
    // `defer_upload` - see this class's top comment; same meaning as create()'s.
    // `bgra` - see create()'s comment, same meaning.
    bool updateSubImage(const uint8_t* data, int data_width, int x_pos, int y_pos, int width, int height, int components, bool alpha_only = false, bool defer_upload = false, bool bgra = false);

    // Completes a deferred create()/updateSubImage() upload - main-thread-only
    // (does the actual UpdateSubresource()/GenerateMips() Context calls).
    // No-op (returns false) if there's nothing pending or the texture was
    // destroyed/recreated since the deferred call.
    bool finalizePendingUpload();

    bool hasPendingUpload() const { return mHasPendingUpload; }

    // S24 (2026-08-16): real D3D11 in-place downscale, replacing what was a
    // permanent DX_RENDER no-op (LLImageGL::scaleDown() always returned
    // false) - the actual mechanism behind the whole VRAM-pressure discard-
    // bias system for already-resident textures, previously dead under
    // DX_RENDER. `src_mip_level` is the ALREADY-COMPUTED mip level (this
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

    // S24 (DX_RENDER, 2026-07-30): D3D11 equivalent of GL's
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

    ID3D11ShaderResourceView* getSRV() const { return mSRV; }

    // S24 (DX_RENDER, 2026-07-30): real "does this hold a created GPU
    // resource" check - used by LLImageGL::getHasGLTexture()'s DX_RENDER
    // branch instead of the GL-only mTexName!=0 sentinel, which under
    // DX_RENDER was only ever set to a fake constant (1), never reflecting
    // whether this DXTexture itself actually has a resource.
    bool isValid() const { return mTexture != nullptr; }

    // S24 (2026-07-25): needed by LLImageGL::readBackRaw()'s DX_RENDER
    // branch (GPU->CPU readback via DXReadback, mirroring GL's
    // glGetTexImage()) - DXReadback::readPixels() takes a raw
    // ID3D11Texture2D* source, not an SRV.
    ID3D11Texture2D* getTexture() const { return mTexture; }

private:
    ID3D11Texture2D* mTexture = nullptr;
    ID3D11ShaderResourceView* mSRV = nullptr;
    bool mGenerateMips = false;

    // Deferred-upload state - see finalizePendingUpload().
    struct PendingUpload
    {
        std::vector<uint8_t> rgba; // tightly packed RGBA8, width*height*4
        int width = 0;
        int height = 0;
        int x = 0; // 0,0 for create()'s full mip-0 upload
        int y = 0;
        bool is_subimage = false; // false = create()'s path (no box), true = updateSubImage()'s box path
    };
    PendingUpload mPending;
    bool mHasPendingUpload = false;
};
