#pragma once
#include <d3d11.h>

// Wraps a genuine D3D11 TextureCubeArray resource - LLCubeMapArray's
// DX_RENDER backend, mirroring DXCubeTexture's single-cubemap pattern but
// for the reflection-probe manager's actual array type (mTexture/
// mIrradianceMaps in llsphereprobes.h).
//
// Unlike DXCubeTexture::copyFace() (which copies an already-uploaded 2D face
// texture from LLCubeMap's own per-face LLImageDX storage), this class's
// slices are populated by copying whatever's CURRENTLY BOUND AS RENDER
// TARGET 0 at call time (copySliceFromBoundRenderTarget()) - mirrors
// DXTexture::copySubImageFromFrameBuffer()'s OMGetRenderTargets()-based
// pattern. This is deliberate: it never needs the destination array bound
// as an SRV during the copy, sidestepping the "resource bound as both OM
// output and SRV input simultaneously" hazard entirely rather than requiring
// careful call ordering to avoid it.
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
    // opinion on probe/face layout, that's llsphereprobes.cpp's
    // job, matching DXCubeTexture::copyFace() taking a raw face index.
    //
    // src_width/src_height: needed because llsphereprobes.cpp's
    // mip-generation loop keeps a single FIXED-size scratch target
    // (mMipChain[0]) bound while only a shrinking top-left sub-region (via
    // RSSetViewports) is rendered into and copied out each iteration -
    // mirroring GL's glCopyTexSubImage3D(..., width, height), which passes
    // the shrinking region size explicitly rather than assuming "whole
    // framebuffer". Passing 0 for either dimension falls back to whole-
    // subresource copy, matching the bound target's full size.
    //
    // Returns false, with the reason logged, when the copy is not valid
    // (bad index, no bound target, format mismatch, or the source does not
    // fit the destination mip). An invalid copy is never issued to the GPU.
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
    UINT mWidth = 0;
    UINT mHeight = 0;
    DXGI_FORMAT mFormat = DXGI_FORMAT_UNKNOWN;
    UINT mMipLevels = 1;
    UINT mArraySize = 0;
    bool mGenerateMips = false;
};
