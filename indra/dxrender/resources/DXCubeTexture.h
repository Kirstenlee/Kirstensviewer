#pragma once
#include <d3d11.h>

// Wraps a genuine D3D11 cubemap resource (6-face array, one shared
// texture + one TEXTURECUBE shader resource view) - LLCubeMap's DX_RENDER
// backend (see llcubemap.h/.cpp). LLCubeMap's own per-face storage
// (mImages[6], one LLImageGL/DXTexture 2D texture per face) is left
// untouched - this class doesn't duplicate the pixel-repack/upload logic
// that already works correctly for 2D textures. Instead, each face is
// populated by copying its already-uploaded 2D DXTexture into the
// matching array slice via CopySubresourceRegion() - cheap (GPU-side copy,
// no CPU readback) and reuses already-correct upload code.
//
// S24 (2026-08-06, task #113): scoped to LLCubeMap (a single cubemap, used
// by the legacy sky-environment "shiny" reflection fallback) - NOT
// LLCubeMapArray (the much larger multi-probe reflection system, which
// needs its own real per-probe cubemap-array resource plus a 900+ line
// GLSL probe-blending shader port neither attempted here). See
// [[project_dxrender_stage8_status]] memory for the full scope split.
class DXCubeTexture
{
public:
    ~DXCubeTexture() { destroy(); }

    // Allocates the 6-slice cube resource (RGBA8, empty/undefined content -
    // face data is filled in afterward via copyFace()). generate_mips
    // reserves the full mip chain (D3D11_RESOURCE_MISC_GENERATE_MIPS) -
    // GenerateMips() must be called explicitly after all 6 faces are
    // copied in, same two-step pattern as DXTexture::create()'s own
    // generate_mips path.
    bool create(int width, int height, bool generate_mips);

    // Copies an already-uploaded 2D face texture (RGBA8, must match this
    // cubemap's width/height - not checked here, caller's responsibility
    // since LLCubeMap's own per-face LLImageGL objects are always created
    // at the same resolution as each other) into array slice `face` (0-5).
    // S24 (2026-08-15, task #194 offshoot): `face` here MUST already be in
    // real D3D11 cubemap slice order (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z) -
    // this is fixed by the D3D11 API itself, not something the caller gets
    // to relabel. The stale version of this comment claimed slice order
    // "matches LLCubeMap::mTargets[]' order (-X,+X,-Y,+Y,-Z,+Z)" - that was
    // the actual bug (every face landing in the wrong slice, GL order
    // passed straight through as if it were D3D11-native order); the only
    // caller (LLCubeMap::init()) now remaps GL face index -> D3D11 slice
    // index before calling this. Copies into mip 0 only - see
    // generateMipMaps() for the rest of the chain. Returns false if this
    // cubemap hasn't been created yet, face is out of range, or
    // face_texture is null.
    bool copyFace(int face, ID3D11Texture2D* face_texture);

    // Fills in the rest of the mip chain from mip 0's now-copied-in
    // content on all 6 faces - call once after all 6 copyFace() calls
    // complete for this update. No-op if this cubemap wasn't created with
    // generate_mips=true.
    void generateMipMaps();

    void destroy();

    ID3D11ShaderResourceView* getSRV() const { return mSRV; }
    bool isValid() const { return mTexture != nullptr; }

private:
    ID3D11Texture2D* mTexture = nullptr;
    ID3D11ShaderResourceView* mSRV = nullptr;
    UINT mMipLevels = 1;
    bool mGenerateMips = false;
};
