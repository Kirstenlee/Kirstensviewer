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
// S24 (2026-08-29, task #278): the std::shared_mutex this class used to
// carry (task #257, 2026-08-24) is gone. It existed solely to make
// mTexture/mSRV safe against a background thread (DXImageThread, dxrender/
// core/DXImageThread.h) concurrently writing them via create()/destroy()
// while the main thread read them every texture bind - real contention, real
// fix at the time. DXImageThread and the whole RenderDXMultiThreadedTextures/
// Media feature were removed entirely in task #260 (r3672, NVIDIA 610.88
// driver bug, 7 repro attempts) - confirmed via source review that nothing
// in this codebase calls into a DXTexture from any thread but the main one
// any more, so the mutex was pure per-bind lock/unlock overhead (getSRV() is
// on the hot path - called every texture bind, every draw call) with zero
// remaining safety value. Removed alongside DXDevice.cpp now finally passing
// D3D11_CREATE_DEVICE_SINGLETHREADED (same confirmation enabled both) - if
// threaded texture uploads are ever attempted again, they need a real
// main-thread-dispatch design, not a mutex slapped back on this class (that
// was never the part of the old feature that actually broke - see task
// #257's own login-deadlock revert history).
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
    // texture already matches this size - a same-size reuse fast-path was
    // tried here (2026-08-24, task #257) and reverted: it broke ordinary
    // texture streaming under RenderDXMultiThreadedTextures (stale content
    // served indefinitely, retry storms, unbounded memory growth) because
    // this nullptr-data path is NOT CEF-specific, and the caller's own
    // completion tracking expects a real create() to actually happen every
    // time it's called.
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
    bool create(const uint8_t* data, int width, int height, int components, bool generate_mips = false, bool alpha_only = false, bool bgra = false);

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
    // `bgra` - see create()'s comment, same meaning.
    bool updateSubImage(const uint8_t* data, int data_width, int x_pos, int y_pos, int width, int height, int components, bool alpha_only = false, bool bgra = false);

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

    ID3D11ShaderResourceView* getSRV() const
    {
        return mSRV;
    }

    // S24 (DX_RENDER, 2026-07-30): real "does this hold a created GPU
    // resource" check - used by LLImageGL::getHasGLTexture()'s DX_RENDER
    // branch instead of the GL-only mTexName!=0 sentinel, which under
    // DX_RENDER was only ever set to a fake constant (1), never reflecting
    // whether this DXTexture itself actually has a resource.
    bool isValid() const
    {
        return mTexture != nullptr;
    }

    // S24 (2026-07-25): needed by LLImageGL::readBackRaw()'s DX_RENDER
    // branch (GPU->CPU readback via DXReadback, mirroring GL's
    // glGetTexImage()) - DXReadback::readPixels() takes a raw
    // ID3D11Texture2D* source, not an SRV.
    ID3D11Texture2D* getTexture() const
    {
        return mTexture;
    }

private:
    // Shared "release whatever GPU resources this instance currently holds"
    // body - create()/createCompressed()/createFloat()/destroy() all call
    // this as their first step. Named "Locked" from when it was the
    // lock-already-held variant of destroy() (task #257) - the locking is
    // gone (see this class's header comment) but the split is still useful
    // as a plain shared helper, so kept as-is.
    void destroyLocked();

    ID3D11Texture2D* mTexture = nullptr;
    ID3D11ShaderResourceView* mSRV = nullptr;
    bool mGenerateMips = false;
};
