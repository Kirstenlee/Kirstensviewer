#pragma once
#include <cstdint>
#include <vector>

// S24 (2026-09-09): real-time BC7 (mode 6 only - single subset, full 8-bit
// RGBA endpoint precision, no partitioning) encoder built on the existing
// KVOpenCL compute wrapper (kvopencl.h, singleton gCL) - reuses its shared
// platform/device/context but never its shared per-frame VFX queue or
// kernel cache (see KVOpenCL::getComputeQueue()/getContext()'s own
// comments for why). Mode 6 only, not a full multi-mode BC7 encoder: this
// is the standard fast-real-time-encoder trade-off (good quality, a
// fraction of the cost of a full mode search) matching the "low overhead"
// requirement this feature was built for over maximizing ratio/quality.
//
// Lives in newview/, not dxrender/, despite producing DXGI-BC7-ready bytes
// consumed by dxrender/resources/DXTexture.cpp - dxrender is a lower-level
// library newview links AGAINST (newview -> dxrender, never the reverse),
// and dxrender's own CMakeLists.txt does not link OpenCL at all (only
// newview/CMakeLists.txt does, via cmake/OpenCL.cmake, for kvopencl.cpp/
// kveffects.cpp). Since this class's only real dependency is KVOpenCL
// (newview-local), it belongs here; DXTexture::createCompressedMips()
// (dxrender) stays OpenCL-agnostic and just receives plain compressed byte
// buffers, exactly like its existing BC1-3 createCompressed() sibling
// already does today regardless of where those bytes came from.
//
// This class does no D3D11 work itself and touches no LLViewerFetchedTexture/
// LLImageGL state - it is a pure CPU-buffer-in, compressed-bytes-out compute
// utility. Safe to call from any single thread once `gCL` has been
// initialized (see encodeMip()'s own comment) - callers on a background
// thread should use the dedicated compute queue/kernel this class owns, not
// KVOpenCL's shared main-thread queue.
class DXBC7Compressor
{
public:
    // One already-block-aligned mip level's worth of compressed BC7 bytes -
    // 16 bytes per 4x4 block, blocks in row-major order (matches D3D11's
    // own expected SysMemPitch/subresource layout for block-compressed
    // formats - no reordering needed by the caller).
    struct CompressedMip
    {
        std::vector<uint8_t> bytes;
        int width = 0;   // original (pre-padding) pixel width
        int height = 0;  // original (pre-padding) pixel height
        int blockWidth = 0;   // width padded up to a multiple of 4
        int blockHeight = 0;  // height padded up to a multiple of 4
    };

    // Encodes one RGBA8 mip level (`width`*`height` uchar4 texels, top-to-
    // bottom row order - same convention as DXTexture::create()'s `data`)
    // to BC7 mode 6. Pads internally (edge-clamped) if width/height aren't
    // already multiples of 4 - the returned CompressedMip still reports the
    // true (unpadded) width/height for the caller's own bookkeeping, but
    // `bytes` covers the padded block grid (blockWidth/blockHeight), which
    // is what actually needs to reach the GPU.
    //
    // Lazily builds this class's own dedicated OpenCL kernel (once, not
    // through KVOpenCL's shared getKernel()/kernelCache) on first call, via
    // gCL.ensureInit()/gCL.getContext()/gCL.getDevice() - safe to call from
    // a background thread as long as no other thread is concurrently
    // calling gCL.init()/ensureInit() for the FIRST time (ordinary run-time
    // use, i.e. once anything has initialized successfully once, is safe -
    // see KVOpenCL::ensureInit()'s own thread-safety comment).
    //
    // Returns an empty CompressedMip (bytes.empty()) on any failure (no
    // usable OpenCL device, kernel build failure, etc.) - callers must treat
    // that as "skip compression, keep the texture uncompressed" rather than
    // an error to surface to the user; this feature is a pure VRAM
    // optimization; correctness/availability must never depend on it.
    static CompressedMip encodeMip(const uint8_t* rgba8, int width, int height);

private:
    static bool ensureKernel();

    static const char* kBC7Mode6KernelSource;
};
