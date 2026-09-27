#pragma once
#include "llpointer.h"
#include <cstdint>

class LLImageDX;

// S24: threading/orchestration layer around DXBC7Compressor's pure encode
// function - builds the raw mip chain (reusing LLImageBase::generateMip(),
// the same box filter LLImageDXT::encodeDXT() uses for legacy S3TC),
// dispatches per-mip encoding to a background LL::ThreadPool (same
// primitive LLImageDecodeThread uses), then applies the result on the main
// thread via LLImageDX::upgradeToCompressedMips() - never touches D3D11
// off the main thread (D3D11_CREATE_DEVICE_SINGLETHREADED).
//
// Uploads uncompressed immediately; upgrades to BC7 in the background a
// few frames later so compression never delays first appearance on screen.
class DXBC7UploadManager
{
public:
    // Call once per frame from the main thread (LLViewerTextureList's
    // per-frame pump) - applies any background compression jobs that
    // finished since the last call. Cheap no-op when nothing is pending.
    static void update();

    // Kicks off a background BC7 compression job for `tex`'s just-uploaded
    // pixel content, IF eligible. Always safe to call unconditionally -
    // internally no-ops (no job dispatched) when: the feature is disabled
    // (RenderCompressTextures setting), `tex` opted out
    // (getAllowCompression()==false, e.g. font glyph atlases), the texture
    // is too small for compression to be worthwhile (padding a tiny texture
    // up to a whole 4x4 BC7 block can cost MORE memory than it saves), or
    // no usable OpenCL device is available.
    //
    // `rgba8` must be the SAME pixel data `tex` was just uploaded with (top-
    // to-bottom row order, 4 bytes/texel - the same convention
    // DXTexture::create()'s own `data` parameter uses) - copied internally,
    // caller's buffer need not stay valid after this call returns.
    static void requestUpgrade(const LLPointer<LLImageDX>& tex, const uint8_t* rgba8, int width, int height);
};
