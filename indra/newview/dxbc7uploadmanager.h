#pragma once
#include "llpointer.h"
#include <cstdint>

class LLImageDX;

// S24: threading/orchestration layer around DXBC7Compressor's pure encode
// function - builds the raw mip chain (reusing LLImageBase::generateMip(),
// the same box filter LLImageDXT::encodeDXT() uses for legacy S3TC),
// dispatches per-mip encoding to a background LL::ThreadPool (same
// primitive LLImageDecodeThread uses), then applies the result on the main
// thread via LLImageDX::upgradeToCompressedMips() - deliberately kept
// main-thread-only by design (not merely because of a now-removed device
// flag - see DXDevice.cpp's own comment on D3D11_CREATE_DEVICE_SINGLETHREADED
// for why that's no longer the real reason anything here stays main-thread).
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

    // Cheap pre-check for requestUpgrade()'s own eligibility gates below -
    // touches no pixel data, so a caller about to do real work to prepare
    // `rgba8` (a repack, an alpha-variance scan, etc.) can skip all of that
    // first if this returns false. requestUpgrade() itself calls this too,
    // so the gates are defined in exactly one place.
    static bool wouldAccept(const LLPointer<LLImageDX>& tex, int width, int height);

    // Kicks off a background BC7 compression job for `tex`'s just-uploaded
    // pixel content, IF eligible (see wouldAccept()) and a usable OpenCL
    // device is available. Always safe to call unconditionally - internally
    // no-ops (no job dispatched) when ineligible.
    //
    // `rgba8` must be the SAME pixel data `tex` was just uploaded with (top-
    // to-bottom row order, 4 bytes/texel - the same convention
    // DXTexture::create()'s own `data` parameter uses) - copied internally,
    // caller's buffer need not stay valid after this call returns.
    static void requestUpgrade(const LLPointer<LLImageDX>& tex, const uint8_t* rgba8, int width, int height);
};
