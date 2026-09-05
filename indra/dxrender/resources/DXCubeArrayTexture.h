#pragma once
#include <d3d11.h>

// Wraps a genuine D3D11 TextureCubeArray resource - LLCubeMapArray's
// DX_RENDER backend, mirroring DXCubeTexture's single-cubemap pattern
// (task #113) but for the reflection-probe manager's actual array type
// (mTexture/mIrradianceMaps in llreflectionmapmanager.h).
//
// S24 (2026-08-09, task #147 step 2): unlike DXCubeTexture::copyFace()
// (which copies an already-uploaded 2D face texture in, one face at a
// time, from LLCubeMap's own per-face LLImageGL storage), this class's
// slices are populated by copying whatever's CURRENTLY BOUND AS RENDER
// TARGET 0 at call time (copySliceFromBoundRenderTarget()) - mirrors
// DXTexture::copySubImageFromFrameBuffer()'s OMGetRenderTargets()-based
// pattern (DXTexture.cpp). This is a deliberate design choice, not an
// oversight: it never needs the destination array bound as an SRV during
// the copy, which sidesteps the "resource bound as both OM output and SRV
// input simultaneously" hazard class entirely (the exact crash class that
// made LLReflectionMapManager::update() gate itself off under DX_RENDER in
// the first place) rather than requiring careful call ordering to avoid it.
class DXCubeArrayTexture
{
public:
    ~DXCubeArrayTexture() { destroy(); }

    // Allocates the (count*6)-slice cube-array resource, empty/undefined
    // content - real data is filled in afterward per-slice via
    // copySliceFromBoundRenderTarget(). hdr selects
    // DXGI_FORMAT_R16G16B16A16_FLOAT vs DXGI_FORMAT_R8G8B8A8_UNORM - see
    // the .cpp for why this deliberately differs from GL's R11F_G11F_B10F.
    // generate_mips reserves the full mip chain (D3D11_RESOURCE_MISC_
    // GENERATE_MIPS) - GenerateMips() must be called explicitly once all
    // slices are populated, same two-step pattern as DXCubeTexture.
    bool create(int width, int height, int count, bool hdr, bool generate_mips);

    // Copies the texture currently bound as render target 0 (via
    // OMGetRenderTargets()) into this array's (mip, arraySlice)
    // subresource. arraySlice is the caller's already-computed
    // "probe_index*6 + face" (or equivalent) index - this class has no
    // opinion on probe/face layout, that's llreflectionmapmanager.cpp's
    // job, matching DXCubeTexture::copyFace() taking a raw face index.
    //
    // S24 (2026-08-10, task #147/#184 follow-up): src_width/src_height
    // ADDED - the original "full-subresource copy, no D3D11_BOX" design
    // assumed the bound render target is always exactly the right size for
    // the destination mip. That's true for llreflectionmapmanager.cpp's
    // per-mip mMipChain[] usage in isolation, but NOT for its actual usage
    // pattern: mMipChain[0] (a single, FIXED-size scratch target) stays
    // bound across an entire mip-generation loop while only a shrinking
    // top-left sub-region (via RSSetViewports) is actually rendered into
    // and meant to be copied out each iteration - exactly mirroring GL's
    // own glCopyTexSubImage3D(..., width, height) call, which explicitly
    // passes the shrinking region size rather than relying on an implicit
    // "whole framebuffer" size. Passing 0 for either dimension falls back
    // to the old whole-subresource behavior (matches the bound target's
    // full size) for any caller where that assumption still genuinely
    // holds.
    bool copySliceFromBoundRenderTarget(int mip, int arraySlice, UINT src_width = 0, UINT src_height = 0);

    // Fills in the rest of the mip chain via the GPU's native mip
    // generation - see DXCubeTexture::generateMipMaps()'s identical
    // pattern. No-op if this array wasn't created with generate_mips=true.
    void generateMipMaps();

    void destroy();

    ID3D11ShaderResourceView* getSRV() const { return mSRV; }
    bool isValid() const { return mTexture != nullptr; }

    // Real mip level count D3D11 actually allocated (queried back via
    // GetDesc() in create() - see its header comment). generate_mips=true
    // requests MipLevels=0 (full auto chain down to 1x1), which for a
    // power-of-two resolution allocates ONE MORE level than
    // floor(log2(width)+0.5) - callers that independently recompute their
    // own "how many mips" count (rather than reading this back) will
    // under-count by one and leave the final mip permanently unwritten.
    UINT getMipLevels() const { return mMipLevels; }

private:
    ID3D11Texture2D* mTexture = nullptr;
    ID3D11ShaderResourceView* mSRV = nullptr;
    UINT mMipLevels = 1;
    UINT mArraySize = 0;
    bool mGenerateMips = false;
};
